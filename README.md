[![Build Status](https://travis-ci.org/stereobooster/syck.svg?branch=master)](https://travis-ci.org/stereobooster/syck)

```
                             . syck .

                         [ version 0.70 ]





INSTALLATION

   ./bootstrap
   ./configure
   make
   make check
   sudo make install

If the unit tests don't pass, notify me immediately.  This distribution
is tested on FreeBSD and Linux.  I don't release it unless the tests
pass on those machines.  If tests aren't passing, then that's a problem.

ABOUT

Syck is the Scripters' YAML Cobble-Yourself-a-Parser Kit.  I don't
much care if the acronym works, as long as the library does!

The whole point of Syck is to make parsing and emitting YAML very
simple for scripting languages through C bindings.  It doesn't strive
to be a pull parser or very extendible.  It just is concerned with
loading a YAML document into a C structure which can be easily
translated into a scripting language's internal native data type.

RUBY INSTALLATION

You don't need to `make install', but please configure and make libsyck
as outlined above.

   cd ext/ruby
   ruby install.rb config
   ruby install.rb setup
   sudo ruby install.rb install

Syck works best with Ruby.  Ruby's symbol table is leveraged, as well
as Ruby's VALUE system.  (You can read more about that below.)

Syck is now included with Ruby (beginning with Ruby 1.8.0.)  Please
voice your support for Syck/YAML in Ruby distributions on the various
platforms.

PYTHON INSTALLATION

You'll need to `make install' as described above.

   cd ext/python/
   python setup.py build
   sudo python setup.py install

PHP INSTALLATION

You'll need to `make install' as described above.

   ln -s lib include    # or cp -r lib include
   cd ext/php/
   phpize
   ./configure --with-syck=../..
   make
   sudo make install

HOW SYCK IS SO GREAT

For example, in Ruby everything evaluates to a VALUE.  I merely
supply a handler to Syck that will take a SyckNode and transform
it into a Ruby VALUE.

A simple Ruby YAML::load could be built like so:

  static VALUE
  YAML_load( VALUE str )
  {
    SyckParser* parser;
    parser = syck_new_parser();
    syck_parser_handler( parser, YAML_handler );
    return syck_parse( parser, str );
  }

  static VALUE
  YAML_handler( SyckNode* node )
  {
    switch( node->kind )
    {
      case SYCK_MAP:
        VALUE key;
        VALUE h = rb_hash_new();
        for ( key = node->content[0]; key != null; key++ )
        {
          rb_hash_set( h, key, key++ );
        }
        return h;
      break;
    }
  }

For most C developers, it should be a no-brainer to bring
basic YAML serialization to PHP, Tcl, Cocoa, etc.

Instructions for using Syck's API are available in the
README.EXT in this very same directory.
```
