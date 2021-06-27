#ifndef LEXER_H
#define LEXER_H

Token nextToken(Context *);
Token peekToken(unsigned int, Context *);

#endif /* LEXER_H */
