//
// syck.c
//
// $Author$
// $Date$
//
// Copyright (C) 2003 why the lucky stiff
//
#include <stdio.h>
#include <string.h>

#include "syck.h"

#define SYCK_YAML_MAJOR 1
#define SYCK_YAML_MINOR 0
#define SYCK_BUFFERSIZE 262144

//
// Custom assert
//
void 
syck_assert( char *file_name, unsigned line_num )
{
    fflush( NULL );
    fprintf( stderr, "\nAssertion failed: %s, line %u\n",
             file_name, line_num );
    fflush( stderr );
    abort();
}

char *
syck_strndup( char *buf, long len )
{
    char *new = S_ALLOC_N( char, len + 1 );
    S_MEMZERO( new, char, len + 1 );
    S_MEMCPY( new, buf, char, len );
}

//
// Default IO functions
//
long
syck_io_file_read( char *buf, SyckIoFile *file, long max_size, long skip )
{
    char *beg;
    long len = 0;

    ASSERT( file != NULL );

    max_size -= skip;
    len = fread( buf + skip, max_size, sizeof( char ), file->ptr );
#if REDEBUG
    printf( "LEN: %d\n", len );
#endif
    len += skip;
    buf[len] = '\0';
#if REDEBUG
    printf( "POS: %d\n", len );
    printf( "BUFFER: %s\n", buf );
#endif
    return len;
}

long
syck_io_str_read( char *buf, SyckIoStr *str, long max_size, long skip )
{
    char *beg;
    long len = 0;

    ASSERT( str != NULL );
    beg = str->ptr;
    if ( max_size >= 0 )
    {
        max_size -= skip;
        if ( max_size < 0 ) max_size = 0;

        str->ptr += max_size;
        if ( str->ptr > str->end )
        {
            str->ptr = str->end;
        }
    }
    else
    {
        // Use exact string length
        while ( str->ptr < str->end ) {
            if (*(str->ptr++) == '\n') break;
        }
    }
    if ( beg < str->ptr )
    {
        len = str->ptr - beg;
        S_MEMCPY( buf + skip, beg, char, len );
    }
#if REDEBUG
    printf( "LEN: %d\n", len );
#endif
    len += skip;
    buf[len] = '\0';
#if REDEBUG
    printf( "POS: %d\n", len );
    printf( "BUFFER: %s\n", buf );
#endif
    return len;
}

void
syck_reset_parser_levels( SyckParser *p )
{
    p->lvl_idx = 1;
    p->levels[0].spaces = -1;
    p->levels[0].domain = "yaml.org,2002/";
    p->levels[0].status = syck_lvl_header;
}

//
// Allocate the parser
//
SyckParser *
syck_new_parser()
{
    SyckParser *p;
    p = S_ALLOC( SyckParser );
    p->lvl_capa = ALLOC_CT;
    p->levels = S_ALLOC_N( SyckLevel, p->lvl_capa ); 
    p->io_type = syck_io_str;
    p->io.str = NULL;
    p->syms = NULL;
    p->anchors = st_init_strtable();
    p->buffer = S_ALLOC_N( char, 16384 );
    p->cursor = NULL;
    p->lineptr = NULL;
    p->token = NULL;
    p->toktmp = NULL;
    p->marker = NULL;
    p->limit = NULL;
    p->linect = 0;
    p->implicit_typing = 1;
    p->taguri_expansion = 0;
    syck_reset_parser_levels( p );
    return p;
}

int
syck_add_sym( SyckParser *p, char *data )
{
    SYMID id = 0;
    if ( p->syms == NULL )
    {
        p->syms = st_init_numtable();
    }
    id = p->syms->num_entries;
    st_insert( p->syms, id, data );
    return id;
}

int
syck_lookup_sym( SyckParser *p, SYMID id, char **data )
{
    if ( p->syms == NULL ) return 0;
    return st_lookup( p->syms, id, data );
}

enum st_retval 
syck_st_free_nodes( char *key, SyckNode *n, char *arg )
{
    syck_free_node( n );
    return ST_CONTINUE;
}

void
syck_free_parser( SyckParser *p )
{
    char *key;
    SyckNode *node;

    //
    // Free the adhoc symbol table
    // 
    if ( p->syms != NULL )
    {
        st_free_table( p->syms );
    }

    //
    // Free the anchor table
    //
    st_foreach( p->anchors, syck_st_free_nodes, NULL );
    st_free_table( p->anchors );

    //
    // Free all else
    //
    S_FREE( p->levels );
    S_FREE( p->buffer );
    free_any_io( p );
    S_FREE( p );
}

void
syck_parser_handler( SyckParser *p, SyckNodeHandler hdlr )
{
    ASSERT( p != NULL );
    p->handler = hdlr;
}

void
syck_parser_implicit_typing( SyckParser *p, int flag )
{
    p->implicit_typing = ( flag == 0 ? 0 : 1 );
}

void
syck_parser_taguri_expansion( SyckParser *p, int flag )
{
    p->taguri_expansion = ( flag == 0 ? 0 : 1 );
}

void
syck_parser_error_handler( SyckParser *p, SyckErrorHandler hdlr )
{
    ASSERT( p != NULL );
    p->error_handler = hdlr;
}

void
syck_parser_file( SyckParser *p, FILE *fp, SyckIoFileRead read )
{
    ASSERT( p != NULL );
    free_any_io( p );
    p->io_type = syck_io_file;
    p->io.file = S_ALLOC( SyckIoFile );
    p->io.file->ptr = fp;
    if ( read != NULL )
    {
        p->io.file->read = read;
    }
    else
    {
        p->io.file->read = syck_io_file_read;
    }
}

void
syck_parser_str( SyckParser *p, char *ptr, long len, SyckIoStrRead read )
{
    ASSERT( p != NULL );
    free_any_io( p );
    p->io_type = syck_io_str;
    p->io.str = S_ALLOC( SyckIoStr );
    p->io.str->beg = ptr;
    p->io.str->ptr = ptr;
    p->io.str->end = ptr + len;
    if ( read != NULL )
    {
        p->io.str->read = read;
    }
    else
    {
        p->io.str->read = syck_io_str_read;
    }
}

void
syck_parser_str_auto( SyckParser *p, char *ptr, SyckIoStrRead read )
{
    syck_parser_str( p, ptr, strlen( ptr ), read );
}

SyckLevel *
syck_parser_current_level( SyckParser *p )
{
    return &p->levels[p->lvl_idx-1];
}

void
syck_parser_pop_level( SyckParser *p )
{
    ASSERT( p != NULL );

    // The root level should never be popped
    if ( p->lvl_idx <= 1 ) return;

    p->lvl_idx -= 1;
    if ( p->levels[p->lvl_idx - 1].domain != p->levels[p->lvl_idx].domain )
    {
        free( p->levels[p->lvl_idx].domain );
    }
}

void 
syck_parser_add_level( SyckParser *p, int len )
{
    ASSERT( p != NULL );
    if ( p->lvl_idx + 1 > p->lvl_capa )
    {
        p->lvl_capa += ALLOC_CT;
        S_REALLOC_N( p->levels, SyckLevel, p->lvl_capa );
    }

    ASSERT( len > p->levels[p->lvl_idx-1].spaces );
    p->levels[p->lvl_idx].spaces = len;
    p->levels[p->lvl_idx].domain = p->levels[p->lvl_idx-1].domain;
    p->levels[p->lvl_idx].status = p->levels[p->lvl_idx-1].status;
    p->lvl_idx += 1;
}

void
free_any_io( SyckParser *p )
{
    ASSERT( p != NULL );
    switch ( p->io_type )
    {
        case syck_io_str:
            if ( p->io.str != NULL ) 
            {
                S_FREE( p->io.str );
                p->io.str = NULL;
            }
        break;

        case syck_io_file:
            if ( p->io.file != NULL ) 
            {
                S_FREE( p->io.file );
                p->io.file = NULL;
            }
        break;
    }
}

long
syck_move_tokens( SyckParser *p )
{
    long count, skip;
    ASSERT( p->buffer != NULL );

    if ( p->token == NULL )
        return 0;

    skip = p->limit - p->token;
    if ( skip < 1 )
        return 0;

#if REDEBUG
    printf( "DIFF: %d\n", skip );
#endif

    if ( ( count = p->token - p->buffer ) )
    {
        S_MEMMOVE( p->buffer, p->token, char, skip );
        p->token = p->buffer;
        p->marker -= count;
        p->cursor -= count;
        p->toktmp -= count;
        p->limit -= count;
        p->lineptr -= count;
    }
    return skip;
}

void
syck_check_limit( SyckParser *p, long len )
{
    if ( p->cursor == NULL )
    {
        p->cursor = p->buffer;
        p->lineptr = p->buffer;
        p->marker = p->buffer;
    }
    p->limit = p->buffer + len;
}

long
syck_parser_read( SyckParser *p )
{
    long len = 0;
    long skip = 0;
    ASSERT( p != NULL );
    switch ( p->io_type )
    {
        case syck_io_str:
            skip = syck_move_tokens( p );
            len = (p->io.str->read)( p->buffer, p->io.str, SYCK_BUFFERSIZE - 1, skip );
            break;

        case syck_io_file:
            skip = syck_move_tokens( p );
            len = (p->io.file->read)( p->buffer, p->io.file, SYCK_BUFFERSIZE - 1, skip );
            break;
    }
    syck_check_limit( p, len );
    return len;
}

long
syck_parser_readlen( SyckParser *p, long max_size )
{
    long len = 0;
    long skip = 0;
    ASSERT( p != NULL );
    switch ( p->io_type )
    {
        case syck_io_str:
            skip = syck_move_tokens( p );
            len = (p->io.str->read)( p->buffer, p->io.str, max_size, skip );
            break;

        case syck_io_file:
            skip = syck_move_tokens( p );
            len = (p->io.file->read)( p->buffer, p->io.file, max_size, skip );
            break;
    }
    syck_check_limit( p, len );
    return len;
}

SYMID
syck_parse( SyckParser *p )
{
    char *line;

    ASSERT( p != NULL );

    yyparse( p );
    syck_reset_parser_levels( p );
    return p->root;
}

void
syck_default_error_handler( SyckParser *p, char *msg )
{
    printf( "Error at [Line %d, Col %d]: %s\n", 
        p->linect,
        p->cursor - p->lineptr,
        msg );
}

