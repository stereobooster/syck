/*
 * rubyext.c
 *
 * $Author$
 * $Date$
 *
 * Copyright (C) 2003 why the lucky stiff
 */

#include "ruby.h"
#include "syck.h"
#include <sys/types.h>
#include <time.h>

#define RUBY_DOMAIN   "ruby.yaml.org,2002"

static ID s_utc, s_at, s_to_f, s_read, s_binmode;
static VALUE sym_model, sym_generic;
static VALUE sym_scalar, sym_seq, sym_map;
VALUE cParser, cLoader, cNode, oDefaultLoader;

/*
 * my private collection of numerical oddities.
 */
static double S_zero()    { return 0.0; }
static double S_one() { return 1.0; }
static double S_inf() { return S_one() / S_zero(); }
static double S_nan() { return S_zero() / S_zero(); }

static VALUE syck_node_transform( VALUE );

SYMID rb_syck_parse_handler _((SyckParser *, SyckNode *));
SYMID rb_syck_load_handler _((SyckParser *, SyckNode *));
void rb_syck_err_handler _((SyckParser *, char *));

/*
 * read from io.
 */
long
rb_syck_io_str_read( char *buf, SyckIoStr *str, long max_size, long skip )
{
    long len = 0;

    ASSERT( str != NULL );
    max_size -= skip;
    if ( max_size < 0 ) max_size = 0;

    if ( max_size > 0 )
    {
        //
        // call io#read.
        //
        VALUE src = (VALUE)str->ptr;
        VALUE n = LONG2NUM(max_size);
        VALUE str = rb_funcall2(src, s_read, 1, &n);
        if (!NIL_P(str))
        {
            len = RSTRING(str)->len;
            memcpy( buf + skip, RSTRING(str)->ptr, len );
        }
    }
    len += skip;
    buf[len] = '\0';
    return len;
}

/*
 * determine: are we reading from a string or io?
 */
void
syck_parser_assign_io(parser, port)
	SyckParser *parser;
	VALUE port;
{
    if (rb_respond_to(port, rb_intern("to_str"))) {
	    //arg.taint = OBJ_TAINTED(port); /* original taintedness */
	    //StringValue(port);	       /* possible conversion */
	    syck_parser_str( parser, RSTRING(port)->ptr, RSTRING(port)->len, NULL );
    }
    else if (rb_respond_to(port, s_read)) {
        if (rb_respond_to(port, s_binmode)) {
            rb_funcall2(port, s_binmode, 0, 0);
        }
        //arg.taint = Qfalse;
	    syck_parser_str( parser, (char *)port, 0, rb_syck_io_str_read );
    }
    else {
        rb_raise(rb_eTypeError, "instance of IO needed");
    }
}

/*
 * Get value in hash by key, forcing an empty hash if nil.
 */
VALUE
syck_get_hash_aref(hsh, key)
    VALUE hsh, key;
{
   VALUE val = rb_hash_aref( hsh, key );
   if ( NIL_P( val ) ) 
   {
       val = rb_hash_new();
       rb_hash_aset(hsh, key, val);
   }
   return val;
}

/*
 * creating timestamps
 */
SYMID
rb_syck_mktime(str)
    char *str;
{
    VALUE time;
    char *ptr = str;
    VALUE year, mon, day, hour, min, sec, usec;

    // Year
    ptr[4] = '\0';
    year = INT2FIX(strtol(ptr, NULL, 10));

    // Month
    ptr += 4;
    while ( !isdigit( *ptr ) ) ptr++;
    mon = INT2FIX(strtol(ptr, NULL, 10));

    // Day
    ptr += 2;
    while ( !isdigit( *ptr ) ) ptr++;
    day = INT2FIX(strtol(ptr, NULL, 10));

    // Hour
    ptr += 2;
    while ( !isdigit( *ptr ) ) ptr++;
    hour = INT2FIX(strtol(ptr, NULL, 10));

    // Minute 
    ptr += 2;
    while ( !isdigit( *ptr ) ) ptr++;
    min = INT2FIX(strtol(ptr, NULL, 10));

    // Second 
    ptr += 2;
    while ( !isdigit( *ptr ) ) ptr++;
    sec = INT2FIX(strtol(ptr, NULL, 10));

    // Millisecond 
    ptr += 2;
    usec = INT2FIX( strtod( ptr, NULL ) * 1000000 );

    // Make UTC time
    time = rb_funcall(rb_cTime, s_utc, 7, year, mon, day, hour, min, sec, usec);

    // Time Zone
    while ( *ptr != 'Z' && *ptr != '+' && *ptr != '-' && *ptr != '\0' ) ptr++;
    if ( *ptr == '-' || *ptr == '+' )
    {
        long tz_offset = 0;
        double utc_time = 0;
        tz_offset += strtol(ptr, NULL, 10) * 3600;

        while ( *ptr != ':' && *ptr != '\0' ) ptr++;
        if ( *ptr == ':' )
        {
            ptr += 1;
            if ( tz_offset < 0 )
            {
                tz_offset -= strtol(ptr, NULL, 10) * 60;
            }
            else
            {
                tz_offset += strtol(ptr, NULL, 10) * 60;
            }
        }

        // Make TZ time
        utc_time = NUM2DBL(rb_funcall(time, s_to_f, 0));
        utc_time -= tz_offset;
        time = rb_funcall(rb_cTime, s_at, 1, rb_float_new(utc_time));
    }

    return time;
}

/*
 * {generic mode} node handler
 * - Loads data into Node classes
 */
SYMID
rb_syck_parse_handler(p, n)
    SyckParser *p;
    SyckNode *n;
{
    VALUE t, v, obj;
    int i;

    obj = rb_obj_alloc(cNode);
    if ( n->type_id != NULL )
    {
        t = rb_str_new2(n->type_id);
        rb_iv_set(obj, "@type_id", t);
    }

    switch (n->kind)
    {
        case syck_str_kind:
            rb_iv_set(obj, "@kind", sym_scalar);
            v = rb_str_new( n->data.str->ptr, n->data.str->len );
        break;

        case syck_seq_kind:
            rb_iv_set(obj, "@kind", sym_seq);
            v = rb_ary_new2( n->data.list->idx );
            for ( i = 0; i < n->data.list->idx; i++ )
            {
                rb_ary_store( v, i, syck_seq_read( n, i ) );
            }
        break;

        case syck_map_kind:
            rb_iv_set(obj, "@kind", sym_map);
            v = rb_hash_new();
            for ( i = 0; i < n->data.pairs->idx; i++ )
            {
                VALUE key = syck_node_transform( syck_map_read( n, map_key, i ) );
                VALUE val = rb_ary_new();
                rb_ary_push(val, syck_map_read( n, map_key, i ));
                rb_ary_push(val, syck_map_read( n, map_value, i ));

                rb_hash_aset( v, key, val );
            }
        break;
    }

	if ( p->bonus != 0 )
	{
		VALUE proc = (VALUE)p->bonus;
		rb_funcall(proc, rb_intern("call"), 1, v);
	}
    rb_iv_set(obj, "@value", v);
    return obj;
}

/*
 * {native mode} node handler
 * - Converts data into native Ruby types
 */
SYMID
rb_syck_load_handler(p, n)
    SyckParser *p;
    SyckNode *n;
{
    VALUE obj;
    long i;
    int str = 0;
    int check_transfers = 0;

    switch (n->kind)
    {
        case syck_str_kind:
            if ( n->type_id == NULL || strcmp( n->type_id, "str" ) == 0 )
            {
                obj = rb_str_new( n->data.str->ptr, n->data.str->len );
            }
            else if ( strcmp( n->type_id, "null" ) == 0 )
            {
                obj = Qnil;
            }
            else if ( strcmp( n->type_id, "bool#yes" ) == 0 )
            {
                obj = Qtrue;
            }
            else if ( strcmp( n->type_id, "bool#no" ) == 0 )
            {
                obj = Qfalse;
            }
            else if ( strcmp( n->type_id, "int#hex" ) == 0 )
            {
                obj = rb_cstr2inum( n->data.str->ptr, 16 );
            }
            else if ( strcmp( n->type_id, "int#oct" ) == 0 )
            {
                obj = rb_cstr2inum( n->data.str->ptr, 8 );
            }
            else if ( strncmp( n->type_id, "int", 3 ) == 0 )
            {
                syck_str_blow_away_commas( n );
                obj = rb_cstr2inum( n->data.str->ptr, 10 );
            }
            else if ( strcmp( n->type_id, "float#nan" ) == 0 )
            {
                obj = rb_float_new( S_nan() );
            }
            else if ( strcmp( n->type_id, "float#inf" ) == 0 )
            {
                obj = rb_float_new( S_inf() );
            }
            else if ( strcmp( n->type_id, "float#neginf" ) == 0 )
            {
                obj = rb_float_new( -S_inf() );
            }
            else if ( strncmp( n->type_id, "float", 5 ) == 0 )
            {
                double f;
                syck_str_blow_away_commas( n );
                f = strtod( n->data.str->ptr, NULL );
                obj = rb_float_new( f );
            }
            else if ( strcmp( n->type_id, "timestamp#iso8601" ) == 0 )
            {
                obj = rb_syck_mktime( n->data.str->ptr );
            }
            else if ( strcmp( n->type_id, "timestamp#spaced" ) == 0 )
            {
                obj = rb_syck_mktime( n->data.str->ptr );
            }
            else if ( strcmp( n->type_id, "timestamp#ymd" ) == 0 )
            {
                S_REALLOC_N( n->data.str->ptr, char, 22 );
                strcat( n->data.str->ptr, "t00:00:00Z" );
                obj = rb_syck_mktime( n->data.str->ptr );
            }
            else if ( strncmp( n->type_id, "timestamp", 9 ) == 0 )
            {
                obj = rb_syck_mktime( n->data.str->ptr );
            }
            else
            {
                check_transfers = 1;
                obj = rb_str_new( n->data.str->ptr, n->data.str->len );
            }
        break;

        case syck_seq_kind:
            obj = rb_ary_new2( n->data.list->idx );
            for ( i = 0; i < n->data.list->idx; i++ )
            {
                rb_ary_store( obj, i, syck_seq_read( n, i ) );
            }
            check_transfers = 1;
        break;

        case syck_map_kind:
            obj = rb_hash_new();
            for ( i = 0; i < n->data.pairs->idx; i++ )
            {
                rb_hash_aset( obj, syck_map_read( n, map_key, i ), syck_map_read( n, map_value, i ) );
            }
            check_transfers = 1;
        break;
    }

	if ( p->bonus != 0 )
	{
		VALUE proc = (VALUE)p->bonus;
		rb_funcall(proc, rb_intern("call"), 1, obj);
	}

    if ( check_transfers == 1 && n->type_id != NULL )
    {
        obj = rb_funcall( oDefaultLoader, rb_intern( "transfer" ), 2, rb_str_new2( n->type_id ), obj );
    }

    return obj;
}

/*
 * friendly errors.
 */
void
rb_syck_err_handler(p, msg)
    SyckParser *p;
    char *msg;
{
    char *endl = p->cursor;

    while ( *endl != '\0' && *endl != '\n' )
        endl++;

    endl[0] = '\0';
    rb_raise(rb_eArgError, "%s on line %d, col %d: `%s'",
           msg,
           p->linect,
           p->cursor - p->lineptr, 
           p->lineptr); 
}

/*
 * data loaded based on the model requested.
 */
void
syck_set_model( parser, model )
	SyckParser *parser;
	VALUE model;
{
	if ( model == sym_generic )
	{
		syck_parser_handler( parser, rb_syck_parse_handler );
		syck_parser_error_handler( parser, rb_syck_err_handler );
		syck_parser_implicit_typing( parser, 1 );
		syck_parser_taguri_expansion( parser, 1 );
	}
	else
	{
		syck_parser_handler( parser, rb_syck_load_handler );
		syck_parser_error_handler( parser, rb_syck_err_handler );
		syck_parser_implicit_typing( parser, 1 );
		syck_parser_taguri_expansion( parser, 0 );
	}
}

/*
 * wrap syck_parse().
 */
static VALUE
rb_run_syck_parse(parser)
    SyckParser *parser;
{
    return syck_parse(parser);
}

/*
 * free parser.
 */
static VALUE
rb_syck_ensure(parser)
    SyckParser *parser;
{
    syck_free_parser( parser );
    return 0;
}

/*
 * YAML::Syck::Parser.new
 */
VALUE 
syck_parser_new(argc, argv, class)
    int argc;
    VALUE *argv;
	VALUE class;
{
	VALUE pobj, options, init_argv[1];
    SyckParser *parser = syck_new_parser();

    rb_scan_args(argc, argv, "01", &options);
	pobj = Data_Wrap_Struct( class, 0, syck_free_parser, parser );

    syck_parser_set_root_on_error( parser, Qnil );

    if ( ! rb_obj_is_instance_of( options, rb_cHash ) )
    {
        options = rb_hash_new();
    }
	init_argv[0] = options;
	rb_obj_call_init(pobj, 1, init_argv);
	return pobj;
}

/*
 * YAML::Syck::Parser.initialize( options )
 */
static VALUE
syck_parser_initialize( self, options )
    VALUE self, options;
{
	rb_iv_set(self, "@options", options);
	return self;
}

/*
 * YAML::Syck::Parser.load( IO or String )
 */
VALUE
syck_parser_load(argc, argv, self)
    int argc;
    VALUE *argv;
	VALUE self;
{
    VALUE port, proc, v, model;
	SyckParser *parser;

    rb_scan_args(argc, argv, "11", &port, &proc);
	Data_Get_Struct(self, SyckParser, parser);
	syck_parser_assign_io(parser, port);

	model = rb_hash_aref( rb_iv_get( self, "@options" ), sym_model );
	syck_set_model( parser, model );

	parser->bonus = 0;
	if ( !NIL_P( proc ) ) 
    {
        parser->bonus = (void *)proc;
    }


    //v = rb_ensure(rb_run_syck_parse, (VALUE)&parser, rb_syck_ensure, (VALUE)&parser);

    return syck_parse( parser );
}

/*
 * YAML::Syck::Parser.load_documents( IO or String ) { |doc| }
 */
VALUE
syck_parser_load_documents(argc, argv, self)
    int argc;
    VALUE *argv;
	VALUE self;
{
    VALUE port, proc, v, model;
	SyckParser *parser;

    rb_scan_args(argc, argv, "1&", &port, &proc);
	Data_Get_Struct(self, SyckParser, parser);
	syck_parser_assign_io(parser, port);

	model = rb_hash_aref( rb_iv_get( self, "@options" ), sym_model );
	syck_set_model( parser, model );
    parser->bonus = 0;

    while ( 1 )
	{
    	v = syck_parse( parser );
        if ( parser->eof == 1 )
        {
            break;
        }
		rb_funcall( proc, rb_intern("call"), 1, v );
	}

    return Qnil;
}

/*
 * YAML::Syck::Loader.initialize
 */
static VALUE
syck_loader_initialize( self )
    VALUE self;
{
    VALUE families;

       rb_iv_set(self, "@families", rb_hash_new() );
    rb_iv_set(self, "@private_types", rb_hash_new() );
    families = rb_iv_get(self, "@families");

    rb_hash_aset(families, rb_str_new2( YAML_DOMAIN ), rb_hash_new());
    rb_hash_aset(families, rb_str_new2( RUBY_DOMAIN ), rb_hash_new());

       return self;
}

/*
 * Add type family, used by add_*_type methods.
 */
VALUE
syck_loader_add_type_family( self, domain, type_re, proc )
    VALUE self, domain, type_re, proc;
{
    VALUE families, domain_types;

    families = rb_iv_get(self, "@families");
    domain_types = syck_get_hash_aref(families, domain);
    rb_hash_aset( domain_types, type_re, proc );
    return Qnil;
}

/*
 * YAML::Syck::Loader.add_domain_type
 */
VALUE
syck_loader_add_domain_type( argc, argv, self )
    int argc;
    VALUE *argv;
       VALUE self;
{
    VALUE domain, type_re, proc, families, ruby_yaml_org, domain_types;

    rb_scan_args(argc, argv, "2&", &domain, &type_re, &proc);
    syck_loader_add_type_family( self, domain, type_re, proc );
    return Qnil;
}


/*
 * YAML::Syck::Loader.add_builtin_type
 */
VALUE
syck_loader_add_builtin_type( argc, argv, self )
    int argc;
    VALUE *argv;
       VALUE self;
{
    VALUE type_re, proc, families, ruby_yaml_org, domain_types;

    rb_scan_args(argc, argv, "1&", &type_re, &proc);
    syck_loader_add_type_family( self, rb_str_new2( YAML_DOMAIN ), type_re, proc );
    return Qnil;
}

/*
 * YAML::Syck::Loader.add_ruby_type
 */
VALUE
syck_loader_add_ruby_type( argc, argv, self )
    int argc;
    VALUE *argv;
       VALUE self;
{
    VALUE type_re, proc, families, ruby_yaml_org, domain_types;

    rb_scan_args(argc, argv, "1&", &type_re, &proc);
    syck_loader_add_type_family( self, rb_str_new2( RUBY_DOMAIN ), type_re, proc );
    return Qnil;
}

/*
 * YAML::Syck::Loader.add_private_type
 */
VALUE
syck_loader_add_private_type( argc, argv, self )
    int argc;
    VALUE *argv;
       VALUE self;
{
    VALUE type_re, proc, priv_types;

    rb_scan_args(argc, argv, "1&", &type_re, &proc);

    priv_types = rb_iv_get(self, "@private_types");
    rb_hash_aset( priv_types, type_re, proc );
    return Qnil;
}

/*
 * YAML::Syck::Loader#detect 
 */
VALUE
syck_loader_detect_implicit( self, val )
    VALUE self, val;
{
    char *type_id;

    if ( TYPE(val) == T_STRING )
    {
        type_id = syck_match_implicit( RSTRING(val)->ptr, RSTRING(val)->len );
        return rb_str_new2( type_id );
    }

    return rb_str_new2( "" );
}

/*
 * iterator to search a type hash for a match.
 */
static VALUE
transfer_find_i(entry, col)
    VALUE entry, col;
{
    VALUE key = rb_ary_entry( entry, 0 );
    VALUE tid = rb_ary_entry( col, 0 );
    VALUE match = rb_funcall(tid, rb_intern("=~"), 1, key);
    if ( ! NIL_P( match ) )
    {
        rb_ary_push( col, rb_ary_entry( entry, 1 ) );
        rb_iter_break();
    }
    return Qnil;
}

/*
 * YAML::Syck::Loader#transfer
 */
VALUE
syck_loader_transfer( self, type, val )
    VALUE self, type, val;
{
    char *taguri = NULL;

       // rb_funcall(rb_mKernel, rb_intern("p"), 2, rb_str_new2( "-- TYPE --" ), type);
    if (NIL_P(type) || !RSTRING(type)->ptr || RSTRING(type)->len == 0) 
    {
        //
        // Empty transfer, detect type
        //
        if ( TYPE(val) == T_STRING )
        {
            taguri = syck_match_implicit( RSTRING(val)->ptr, RSTRING(val)->len );
            taguri = syck_taguri( YAML_DOMAIN, taguri, strlen( taguri ) );
        }
    }
    else
    {
        taguri = syck_type_id_to_uri( RSTRING(type)->ptr );
    }

    if ( taguri != NULL )
    {
        VALUE scheme, name, type_hash, type_proc;
        VALUE type_uri = rb_str_new2( taguri );
        VALUE str_taguri = rb_str_new2("taguri");
        VALUE str_xprivate = rb_str_new2("x-private");
        VALUE parts = rb_str_split( type_uri, ":" );
               // rb_funcall(rb_mKernel, rb_intern("p"), 1, parts);

        scheme = rb_ary_shift( parts );

        if ( rb_str_cmp( scheme, str_xprivate ) == 0 )
        {
            name = rb_ary_join( parts, rb_str_new2( ":" ) );
            type_hash = rb_iv_get(self, "@private_types");
        }
        else if ( rb_str_cmp( scheme, str_taguri ) == 0 )
        {
            VALUE domain = rb_ary_shift( parts );
            name = rb_ary_join( parts, rb_str_new2( ":" ) );
            type_hash = rb_iv_get(self, "@families");
            type_hash = rb_hash_aref(type_hash, domain);
        }
        else
        {
               rb_raise(rb_eTypeError, "invalid typing scheme: %s given",
                       scheme);
        }

        if ( rb_obj_is_instance_of( type_hash, rb_cHash ) )
        {
            type_proc = rb_hash_aref( type_hash, name );
            if ( NIL_P( type_proc ) )
            {
                VALUE col = rb_ary_new();
                rb_ary_push( col, name );
                rb_iterate(rb_each, type_hash, transfer_find_i, col );
                name = rb_ary_shift( col );
                type_proc = rb_ary_shift( col );
            }
                   // rb_funcall(rb_mKernel, rb_intern("p"), 2, name, type_proc);
        }

        if ( rb_obj_is_instance_of( type_proc, rb_cProc ) )
        {
                   val = rb_funcall(type_proc, rb_intern("call"), 2, type_uri, val);
        }
    }

    return val;
}

/*
 * YAML::Syck::Node.initialize
 */
VALUE
syck_node_initialize( self, type_id, val )
    VALUE self, type_id, val;
{
    rb_iv_set( self, "@type_id", type_id );
    rb_iv_set( self, "@value", val );
    return self;
}

VALUE
syck_node_thash( entry, t )
    VALUE entry, t;
{
    VALUE key, val;
    key = rb_ary_entry( entry, 0 );
    val = syck_node_transform( rb_ary_entry( rb_ary_entry( entry, 1 ), 1 ) );
    rb_hash_aset( t, key, val );
    return Qnil;
}

VALUE
syck_node_ahash( entry, t )
    VALUE entry, t;
{
    VALUE val = syck_node_transform( entry );
    rb_ary_push( t, val );
    return Qnil;
}

/*
 * YAML::Syck::Node.transform
 */
VALUE
syck_node_transform( self )
    VALUE self;
{
    VALUE t = Qnil;
    VALUE type_id = rb_iv_get( self, "@type_id" );
    VALUE val = rb_iv_get( self, "@value" );
    if ( rb_obj_is_instance_of( val, rb_cHash ) )
    {
        t = rb_hash_new();
        rb_iterate( rb_each, val, syck_node_thash, t );
    }
    else if ( rb_obj_is_instance_of( val, rb_cArray ) )
    {
        t = rb_ary_new();
        rb_iterate( rb_each, val, syck_node_ahash, t );
    }
    else
    {
        t = val;
    }
    return rb_funcall( oDefaultLoader, rb_intern( "transfer" ), 2, type_id, t );
}

/*
 * Initialize Syck extension
 */
void
Init_syck()
{
    VALUE rb_yaml = rb_define_module( "YAML" );
    VALUE rb_syck = rb_define_module_under( rb_yaml, "Syck" );
    rb_define_const( rb_syck, "VERSION", rb_str_new2( SYCK_VERSION ) );

	//
	// Global symbols
	//
    s_utc = rb_intern("utc");
    s_at = rb_intern("at");
    s_to_f = rb_intern("to_f");
    s_read = rb_intern("read");
    s_binmode = rb_intern("binmode");
	sym_model = ID2SYM(rb_intern("Model"));
	sym_generic = ID2SYM(rb_intern("Generic"));
    sym_map = ID2SYM(rb_intern("map"));
    sym_scalar = ID2SYM(rb_intern("scalar"));
    sym_seq = ID2SYM(rb_intern("seq"));

	//
    // Define YAML::Syck::Loader class
    //
    cLoader = rb_define_class_under( rb_syck, "Loader", rb_cObject );
    rb_define_attr( cLoader, "families", 1, 1 );
    rb_define_attr( cLoader, "private_types", 1, 1 );
    rb_define_method( cLoader, "initialize", syck_loader_initialize, 0 );
    rb_define_method( cLoader, "add_domain_type", syck_loader_add_domain_type, -1 );
    rb_define_method( cLoader, "add_builtin_type", syck_loader_add_builtin_type, -1 );
    rb_define_method( cLoader, "add_ruby_type", syck_loader_add_ruby_type, -1 );
    rb_define_method( cLoader, "add_private_type", syck_loader_add_private_type, -1 );
    rb_define_method( cLoader, "detect_implicit", syck_loader_detect_implicit, 1 );
    rb_define_method( cLoader, "transfer", syck_loader_transfer, 2 );

    oDefaultLoader = rb_funcall( cLoader, rb_intern( "new" ), 0 );
    rb_define_const( rb_syck, "DefaultLoader", oDefaultLoader );

    //
	// Define YAML::Syck::Parser class
	//
    cParser = rb_define_class_under( rb_syck, "Parser", rb_cObject );
    rb_define_attr( cParser, "options", 1, 1 );
	rb_define_singleton_method( cParser, "new", syck_parser_new, -1 );
    rb_define_method(cParser, "initialize", syck_parser_initialize, 1);
    rb_define_method(cParser, "load", syck_parser_load, -1);
    rb_define_method(cParser, "load_documents", syck_parser_load_documents, -1);

    //
    // Define YAML::Syck::Node class
    //
    cNode = rb_define_class_under( rb_syck, "Node", rb_cObject );
    rb_define_attr( cNode, "kind", 1, 1 );
    rb_define_attr( cNode, "type_id", 1, 1 );
    rb_define_attr( cNode, "value", 1, 1 );
    rb_define_attr( cNode, "anchor", 1, 1 );
    rb_define_method( cNode, "initialize", syck_node_initialize, 2);
    rb_define_method( cNode, "transform", syck_node_transform, 0);
}

