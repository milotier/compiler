# Basic compiler

This is a basic prototype compiler I built a while ago in C, for a simple language.
Currently it only has a lexer, parser and (incomplete) typechecker, no code generation.
It can be compiled using the Makefile, after which it can be ran by giving the file to
compile as a command-line argument. This will cause the file to be parsed and 
typechecked and the resulting AST to be pretty-printed to the standard output.
An example source file (`test`) is provided.
