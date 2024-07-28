/*

This file is part of "MxRegex" library

"MxRegex" is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU General Public License
and GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>

*/

#define MXREGEX_DEBUG 1


#if MXREGEX_DEBUG

#include <iostream>
#include <windows.h>

#endif



#include "mxRegex.h"





// VARS

MXREGEX_M m;


#if MXREGEX_DEBUG

char buf[1024];
UInt16 step;

#endif


// CONST


// predefined charsets (non hardcoded)

#if CONST_CHARSET

const CHARSET C_WORD_CHARSET = { {0x00000000, 0x03ff0000, 0x87fffffe, 0x07fffffe, 0x00000000, 0x00000000, 0x00000000, 0x00000000} };
const CHARSET C_DIGIT_CHARSET = { {0x00000000, 0x03ff0000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000} };
const CHARSET C_WHITESPACE_CHARSET = { {0x00003e00, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000} };
const CHARSET C_DOT_CHARSET = { {0xfffffffe, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff } };

#else

const char* const C_WORD_CHARSET_STR = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";
const char* const C_DIGIT_CHARSET_STR = "0123456789";
const char* const C_WHITESPACE_CHARSET_STR = " \t\r\n\v\f";

#endif

const char* const C_ANCHOR_NO_ESC_CHARSET = "bB";                           // anchor after escape (i.e. no $ ^)
const char* const C_ANCHOR_META_NO_ESC_CHARSET = "wWdDsSh";                  // metaclass after escape (i.e. no .)


// CODE




// dummy for debug

#if MXREGEX_DEBUG

void Nop()
{
}

#endif





// convert uppercase  a-z -> A-Z (may be replaced by library)

char Upper(const char c)
{
    if (c >= 'a' && c <= 'z')
        return c - ('a' - 'A');
    return c;
}



// check if digit (may be replaced by library)

UInt8 IsDigit(char c)
{
    return c >= '0' && c <= '9';
}



// convert 1 hex char to dec
// parm
//  c       hex char '0'..'9' | 'A'..'F' | 'a'..'f'
// ret
//  hex value 0..15,  0xff fail

UInt8 HexChar2num(const char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'A' && c <= 'F')
        return c - ('A' + 10);

    if (c >= 'a' && c <= 'f')
        return c - ('a' + 10);

    return 0xff;
}





// convert 2 hex digit to number
// parm
//  str     ptr to 2 digit hex
//  retNum  ptr to returned value 0..255
// ret
//  0: fail, not a hex number
//  1: ok, retNum updated

UInt8 GetHex(const char* str, UInt16* retNum)
{
    UInt8 th;
    UInt8 tl;

    th = HexChar2num(*str);
    if (th > 0x0f)
        return 0;
    str++;

    tl = HexChar2num(*str);
    if (tl > 0x0f)
        return 0;

    *retNum = (th << 4) | tl;
    return 1;
}



// check if a string contains char c (may be replaced by library)

UInt8 CharInStr(const char c, const char* strP)
{
    for (; *strP != '\0'; strP++)
        if (c == *strP)
            return 1;

    return 0;
}




//
// BACKTRACK
//
//
// We keep track of last MAX_BACKTRACK quantifiers of interest, wehere minOcc != maxOcc
// bactrack position refers to 1st char after atom, e.g. regex"[ab]*cd", atom "[ab]*", position is &"c"


// add a backtrack element
// if new element, set counter to max range: will be updated by parser, otherwise keep current counter.
// note: backtrack won't have duplicated regexParseP
// parm
//  regexP  position of backtrack
// ret
//  1 ok, 0 fail ovf

UInt8 BacktrackAdd(const char* regexParseP)
{
    BACKTRACK* bP;
    UInt16 t;

    for (t = 0; t < m.backtrackNum; t++)                    // check if exists, search for leftmost element
    {
        bP = &m.backtrack[t];
        if (bP->regexParseP == regexParseP)                 // if position already present, already ok
            return 1;
    }

    // add new position

    if (m.backtrackNum >= MAX_BACKTRACK - 1)
    {
        m.retSts = REGEXSTS_BACKTRACK_OVF;
        return 0;
    }

    bP = &m.backtrack[m.backtrackNum++];                     // set new position

#if MXREGEX_DEBUG
    snprintf(buf, sizeof(buf), "- BacktrackAdd num: %d, regexP: %s\r\n", m.backtrackNum, regexParseP);
    OutputDebugStringA((LPCSTR)buf);
#endif

    bP->regexParseP = regexParseP;
    bP->minOcc = 0;                                         // set init values, ok
    bP->maxOcc = BACKTRACK_MAXOCC;
    return 1;

}



// get backtrack element, if exists
// parm
//  parseP  position of backtrack
// ret
//  ptr to backtrack element, 0 if not found

BACKTRACK* BacktrackGet(const char* regexParseP)
{
    BACKTRACK* bP;
    UInt16 t;

    for (t = 0; t < m.backtrackNum; t++)
    {
        bP = &m.backtrack[t];
        if (bP->regexParseP == regexParseP)
            return bP;
    }

    return 0;                                               // not found
}



void AltSegmRemoveAt(const char* regexP);



// update backtracks for next iteration
//
// - sort backtrack items by parseP (i.e. regex left to right)
// - search for last updatable element (right to left) at the right regexP
// - if found, update element and restart counter of all elements at the right of it (i.e. nested)
//      if not found, no need for new iteration
// parm
//  parseP  ptr from where which elements will be removed
// ret
//  1 elements updated, require reparse
//  0 no change, reparse not necessary

UInt8 BacktrackIterate(const char* regexParseP)
{
    BACKTRACK* bP;
    const char* cP;
    UInt16 t1;
    UInt16 t2;

    if (m.backtrackNum == 0)                                    // if no backtrack, nothing to do
        return 0;

    // search for rightmost backtrack

    cP = 0;
    t2 = MAX_BACKTRACK;                                         // deflt: not found

    for (t1 = 0; t1 < m.backtrackNum; t1++)
    {
        bP = &m.backtrack[t1];

        if (bP->regexParseP < regexParseP)                      // if before regexP, ignore
            continue;

        if (bP->maxOcc != BACKTRACK_MAXOCC                      // if backtrack evaluated once
            && bP->maxOcc > bP->minOcc                          // and needs new iteration, restart at right
            && bP->regexParseP > cP)                            // and mostright right: save
        {
            t2 = t1;
            cP = bP->regexParseP;
        }
    }

    if (t2 >= MAX_BACKTRACK)                                    // no backtrack: complete, nothing to do
        return 0;

    // do backtrack and invalidate all backtrack after cP

    m.backtrack[t2].maxOcc--;
    AltSegmRemoveAt(cP);                                        // reevaluate alt segm after backtrack
#if MXREGEX_DEBUG
    snprintf(buf, sizeof(buf), "- BacktrackIterate %s, reset altSegm, new maxOcc %d\r\n", cP, m.backtrack[t2].maxOcc);
    OutputDebugStringA((LPCSTR)buf);
#endif

    for (t1 = 0; t1 < m.backtrackNum; t1++)
    {
        bP = &m.backtrack[t1];
        if (bP->regexParseP > cP)
            bP->maxOcc = BACKTRACK_MAXOCC;
    }

    return 1;                                                   // no need for new iteration
}








// check if char after \ is a meta anchor (excluding $^ that are not escaped \)
// Anchor chars should be handled by MxRegex_() see "case ATOMTYPE_ANCHOR:"

UInt8 IsAnchor(const char c)
{
    return CharInStr(c, C_ANCHOR_NO_ESC_CHARSET);
}




// check if char after \ is a metaclass (escluding . that is not excaped \)
// Metaclass must be handled by Atom_charsetAddClass()

UInt8 IsMetaclass(const char c)
{
    return CharInStr(c, C_ANCHOR_META_NO_ESC_CHARSET);
}





//
// atom charset handling
//

// following functions use working charset m.atom.charset


// reset charset

void Atom_charsetResetAll()
{
    register UInt8 t;

    for (t = 0; t < sizeidx_(m.atom.charset.map); t++)
        m.atom.charset.map[t] = 0L;

    return;
}



// add char to charset

void Atom_charsetAddChar(const char c)
{
    m.atom.charset.map[c / 32] |= 1L << (c & 31);
    return;
}



// remove char from charset

void Atom_charsetRemoveChar(const char c)
{
    m.atom.charset.map[c / 32] &= ~(1L << (c & 31));
    return;
}



// add string to charset

void Atom_charsetAddStr(const char* strP)
{
    for (; *strP != '\0'; strP++)
        Atom_charsetAddChar(*strP);
    return;
}



// invert (negate) charse

void Atom_charsetInvert()
{
    register UInt8 t;

    for (t = 0; t < sizeidx_(m.atom.charset.map); t++)
        m.atom.charset.map[t] = ~m.atom.charset.map[t];

    m.atom.charset.map[0] &= ~1L;                    // \0 cannot be in charset

    return;
}



// merge charset to working charset

void Atom_charsetMerge(const CHARSET* charsetP)
{
    register UInt8 t;

    for (t = 0; t < sizeidx_(m.atom.charset.map); t++)
        m.atom.charset.map[t] |= charsetP->map[t];

    return;
}



// export (copy) from working charset

void Atom_charsetExport(CHARSET* charsetDstP)
{
    register UInt8 t;

    for (t = 0; t < sizeidx_(charsetDstP->map); t++)
        charsetDstP->map[t] = m.atom.charset.map[t];

    return;
}



// import (copy) to working charset

void Atom_charsetImport(const CHARSET* charsetSrcP)
{
    register UInt8 t;

    for (t = 0; t < sizeidx_(m.atom.charset.map); t++)
        m.atom.charset.map[t] = charsetSrcP->map[t];

    return;
}



// check if char is in charset

UInt8 Atom_charInCharset(const CHARSET* charsetP, const char c)
{
    if (((charsetP->map[c / 32]) & (1L << (c & 31))))
        return 1;
    return 0;
}



// functions related to regex atoms


// check if char is in \w charset

UInt8 IsWord(const char c)
{
#if CONST_CHARSET
    return Atom_charInCharset(&C_WORD_CHARSET, c);
#else
    return Atom_charInCharset(&m.charset_word, c);
#endif
}







// parse unsigned UInt16 from string, till non-digit char, max 5 digit
// no overflow check
// parm
//  strP        ptr to begin of numeric string \d+
//  &retLenP    RET ptr to number of char parsed
// ret
//  parsed number
//  retLen number of char parsed; 0 = fail

UInt16 ParseInt(const char* strP, UInt16* retLenP)
{
    UInt16 accu;

    *retLenP = 0;
    if (!IsDigit(*strP))
        return 0;
    accu = *strP - '0';
    (*retLenP)++;
    strP++;

    while (IsDigit(*strP))
    {
        accu = accu * 10 + (*strP - '0');
        (*retLenP)++;
        strP++;
    }

    return accu;
}





// add escaped metaclass char \w\W\d\D.. to atom charset.
// '.' not handled here (not excaped)
// parm
//  c  metaclass char

void Atom_charsetAddClass(const char c)
{
    static CHARSET csTmp;

    switch (c)
    {
    case 'w':                   // \w: "a-zA-Z0-9_" + accented (if in charset)
#if CONST_CHARSET
        Atom_charsetMerge(&C_WORD_CHARSET);
#else
        Atom_charsetMerge(&m.charset_word);
#endif
        break;

    case 'W':                   // \W: ^\w
        Atom_charsetExport(&csTmp);
#if CONST_CHARSET
        Atom_charsetImport(&C_WORD_CHARSET);
#else
        Atom_charsetImport(&m.charset_word);
#endif
        Atom_charsetInvert();
        Atom_charsetMerge(&csTmp);
        break;

    case 'd':                   // \d: "0-9"
#if CONST_CHARSET
        Atom_charsetMerge(&C_DIGIT_CHARSET);
#else
        Atom_charsetMerge(&m.charset_digit);
#endif
        break;

    case 'D':                   // \D: ^\d
        Atom_charsetExport(&csTmp);
#if CONST_CHARSET
        Atom_charsetImport(&C_DIGIT_CHARSET);
#else
        Atom_charsetImport(&m.charset_digit);
#endif
        Atom_charsetInvert();
        Atom_charsetMerge(&csTmp);
        break;

    case 'h':                   // \h: "0-9A-Fa-f"   hex char (NON STANDARD)
#if CONST_CHARSET
        Atom_charsetMerge(&C_DIGIT_CHARSET);
#else
        Atom_charsetMerge(&m.charset_digit);
#endif
        Atom_charsetAddStr("abcdefABCDEF");
        break;

    case 's':                   // \s: whitespace tab cr lf vt ff
#if CONST_CHARSET
        Atom_charsetMerge(&C_WHITESPACE_CHARSET);
#else
        Atom_charsetMerge(&m.charset_whitespace);
#endif
        break;

    case 'S':                   // \S: ^\s
        Atom_charsetExport(&csTmp);
#if CONST_CHARSET
        Atom_charsetImport(&C_WHITESPACE_CHARSET);
#else
        Atom_charsetImport(&m.charset_whitespace);
#endif
        Atom_charsetInvert();
        Atom_charsetMerge(&csTmp);
        break;

    default:                    // unhandled, ignore
        break;

    }

    return;
}





// parse quantifier ? * + {min[,max]} in Atom.
// if present, add a backtrack position
// parm:
//  charP       ptr to first possible quantifier char
//  retLenP     RET pto to returned number of char parsed
// ret:
//  1: quantifier found, set retLenP
//  0: charP is not a quantifier, or syntax error (check m.retStatus != REGEXSTS_OK)
// NOTE condition min > max will return error

UInt8 Atom_ParseQtf(const char* charP, UInt16* retLenP)
{
    const char* baseP;
    UInt16 t;

    baseP = charP;                                                  // salva base per calcolo len alla fine
    *retLenP = 0;                                                   // default: sintassi non riconosciuta

    switch (*charP++)
    {
    case '?':                                                       // quantifier ?

        m.atom.minOcc = 0;
        m.atom.maxOcc = 1;
        break;


    case '*':                                                       // quantifier *

        m.atom.minOcc = 0;
        m.atom.maxOcc = 0xffff;
        break;


    case '+':                                                       // quantifier +

        m.atom.minOcc = 1;
        m.atom.maxOcc = 0xffff;
        break;


    case '{':                                                       // quantifier {min[,[max]]}

        m.atom.minOcc = ParseInt(charP, &t);                        // {min
        m.atom.maxOcc = m.atom.minOcc;                              // default

        if (t == 0)
        {
            m.retSts = REGEXSTS_QUANTIFIER_ERR;
            return 0;
        }
        charP += t;
        if (*charP == ',')                                          // if {min,
        {
            charP++;
            if (*charP == '}')                                      // if {min,}
            {
                m.atom.maxOcc = 0xffff;
                charP++;
                break;
            }
            m.atom.maxOcc = ParseInt(charP, &t);                    // {min,max
            if (t == 0)
            {
                m.retSts = REGEXSTS_QUANTIFIER_ERR;
                return 0;
            }
            charP += t;
        }
        if (*charP++ == '}')                                        // check for closing }
            break;

        m.retSts = REGEXSTS_SYNTAX;
        return 0;                                                   // missing }, fail


    case '}':                                                       // not expected, fail

        m.retSts = REGEXSTS_SYNTAX;
        return 0;                                                   // manca }, fail


    default:                                                        // unexpected char, no quantifier: set {1,1}, ok

        m.atom.minOcc = 1;
        m.atom.maxOcc = 1;
        return 0;

    }// switch

    if (m.atom.minOcc > m.atom.maxOcc)                              // if minOcc > maxOcc, fail
    {
        m.retSts = REGEXSTS_QUANTIFIER_ERR;
        return 0;
    }

    *retLenP = charP - baseP;
    return 1;

}



// REGEX PARSER


// get regex atom that may be used to match 1 char in str, with possible quantifier.
// If it's a metaclass like \d or [ab], return charset
// Here we handle backtrack save/check
// parm
//  charP   prt to regex atom
// ret
//  REGEXSTS_OK: ok, atom descriptors stored in m.atom, m.atom.endP updated to ptr to next atom
//  else: fatal error

REGEX_STS GetRegexAtom(const char* charP, const UInt8 isCI)
{
    UInt16 t;

    // init atom default results

    m.atom.type = ATOMTYPE_CHAR;
    m.atom.minOcc = 1;
    m.atom.maxOcc = 1;

    // categorize regex char. Exit switch with break if quantifier may follow, otherwise just save endP and return

    switch (m.atom.c = *charP++)                          // save regex char and move to next
    {

    case '\0':                                              // EOS

        m.atom.type = ATOMTYPE_EOS;
        m.atom.endP = charP;                             // set endP to \0 (len=0)
        return REGEXSTS_OK;


    case '\\':                                           // \ escape

        m.atom.c = *charP++;                             // save escaped char

        if (m.atom.c == '\0')                            // if got EOS: fail
        {
            m.atom.endP = charP;
            return REGEXSTS_SYNTAX;
        }

        if (m.atom.c == 'x' && GetHex(charP, &t))         // if \xHH char hex code
        {
            m.atom.c = (char)t;
            charP += 2;
            break;
        }

        if (IsMetaclass(m.atom.c))                       // if simple metaclass (only one charset)
        {
            Atom_charsetResetAll();
            Atom_charsetAddClass(m.atom.c);              // save charset
            m.atom.type = ATOMTYPE_METACLASS;
            break;
        }

        if (IsAnchor(m.atom.c))                          // if anchor: return to caller
        {
            m.atom.type = ATOMTYPE_ANCHOR;
            m.atom.endP = charP;
            return REGEXSTS_OK;                             // no quantifier
        }

        break;                                              // simple char escaped, get quantifier


    case '[':                                               // CHARSET [^ac-e\d]

        Atom_charsetResetAll();                             // init set (empty)
        m.atom.charsetIsNegate = 0;                         // no negation [^
        m.atom.charsetAllowNegate = 1;                      // waiting 1st char for possible negation
        m.atom.gotMinus = 0;                                // no minus
        m.atom.charsetLastChar = 0;                         // no from-char in char range

        // parse charset [..]

        for (;; charP++)
        {
            if (*charP == ']')                              // if end of charset, done: check quantifier
            {
                if (m.atom.gotMinus)                        // if char range - pending, treat - as simple char and add to charset
                    Atom_charsetAddChar('-');
                charP++;                                    // skip ]
                break;                                      // THIS IS THE ONLY EXIT POINT FOR CHARSET
            }

            if (*charP == '\0')                             // if EOS reached before ], fail
            {
                m.atom.endP = charP;                        // return ptr to \0
                return REGEXSTS_SYNTAX;
            }

            if (*charP == '[' || *charP == '(')             // ( [ must be escaped, fail
            {
                m.atom.endP = charP;
                return REGEXSTS_SYNTAX;
            }

            if (*charP == '^')
            {
                if (m.atom.charsetAllowNegate)              // if ^ is 1st char in set, negate at the end
                {
                    m.atom.charsetAllowNegate = 0;
                    m.atom.charsetIsNegate = 1;
                    continue;
                }
                goto BR_CHARSET_ADDCHAR;
            }

            m.atom.charsetAllowNegate = 0;                  // if got here, ^ will be a simple char

            if (*charP == '-')                              // char range A-B
            {
                if (!IsWord(m.atom.charsetLastChar))        // if previous char was not a word, treat as simple char
                    goto BR_CHARSET_ADDCHAR;
                m.atom.gotMinus = 1;                        // else prepare for char range
                continue;
            }

            if (*charP == '\\')                             // ESCAPED CHAR
            {
                charP++;                                    // skip ESC

                if (*charP == '\0')                         // if \0 EOS: fail
                {
                    m.atom.endP = charP;
                    return REGEXSTS_SYNTAX;
                }

                if (m.atom.gotMinus)                        // if preceded by '-': fail
                {
                    m.atom.endP = charP;
                    return REGEXSTS_SYNTAX;
                }

                if (IsMetaclass(*charP))                    // if metaclass, add class to charset
                {
                    m.atom.charsetLastChar = 0;             // after metaclass, char range not allowed
                    Atom_charsetAddClass(*charP);
                    continue;
                }

                // here escaped char is a simple char, just continue processing

            }//if esc

            // SIMPLE CHAR

            if (m.atom.gotMinus)                            // if was a char range
            {
                if (IsWord(*charP))                         // if -to char is word
                {
                    if (*charP < m.atom.charsetLastChar)    // if bad order: fail
                    {
                        m.atom.endP = charP;
                        return REGEXSTS_SYNTAX;
                    }

                    if (isCI)
                    {
                        for (t = m.atom.charsetLastChar; t <= (UInt16)(*charP); t++)
                            Atom_charsetAddChar(Upper((char)t));       // add range to charset
                    }
                    else
                    {
                        for (t = m.atom.charsetLastChar; t <= (UInt16)(*charP); t++)
                            Atom_charsetAddChar((char)t);       // add range to charset
                    }

                    m.atom.gotMinus = 0;                    // invalidate this char range
                    m.atom.charsetLastChar = 0;
                    continue;
                }
            }

        BR_CHARSET_ADDCHAR:                                 // *** entrypoint add simple char to charset

            if (isCI)
                Atom_charsetAddChar(Upper(*charP));         // add char to charset
            else
                Atom_charsetAddChar(*charP);                // add char to charset
            m.atom.charsetLastChar = *charP;                // save char for possible char range
            m.atom.gotMinus = 0;                            // wait for next -

        }// while 1

        if (m.atom.charsetIsNegate)                         // if negated [^..], adj
            Atom_charsetInvert();

        m.atom.type = ATOMTYPE_METACLASS;
        break;


    case '(':                                               // metachar (

        m.atom.endP = charP;
        m.atom.type = ATOMTYPE_BRACKETOPEN;
        return REGEXSTS_OK;                                 // no quantifier, ok


    case ')':                                               // metachar ) with possible quantifier

        m.atom.type = ATOMTYPE_BRACKETCLOSE;
        break;


    case '.':                                               // metaclass . (any char)

#if CONST_CHARSET
        Atom_charsetImport(&C_DOT_CHARSET);
#else
        Atom_charsetImport(&m.charset_dot);
#endif
        if (!m.isSingleLine)                                 // if NOT mode "single line", remove [\r\n] to set
        {
            Atom_charsetRemoveChar('\r');
            Atom_charsetRemoveChar('\n');
        }
        m.atom.type = ATOMTYPE_METACLASS;
        break;


    case '^':                                               // anchor str begin
    case '$':                                               // anchor str end

        m.atom.endP = charP;
        m.atom.type = ATOMTYPE_ANCHOR;
        return REGEXSTS_OK;                                 // no quantifier, ok


    case '|':                                               // | alternative segment, detected here if previous regex segment was a match

        m.atom.endP = charP;
        m.atom.type = ATOMTYPE_PIPE;
        return REGEXSTS_OK;                                 // no quantifier, ok



    case '{':                                               // quantifier: non expected here, fail
    case '+':
    case '?':
    case '*':

        m.atom.endP = charP;
        return REGEXSTS_SYNTAX;


    default:                                                // simple char

        break;                                              // already in atom.c, check quantifier

    }// switch

    // here there could be a quantifier

    if (Atom_ParseQtf(charP, &t))                           // if valid quantifier: skip it
    {
        charP += t;
        if (m.atom.minOcc < m.atom.maxOcc)                  // if it's a possible trackback position, add if necessary
            BacktrackAdd(charP);
    }
    else                                                    // no valid quantifier, check for errors
    {
        if (m.retSts != REGEXSTS_OK)
            return m.retSts;
    }

    m.atom.endP = charP;                                     // no errors: save ptr to next atom, ok
    return REGEXSTS_OK;

}






//
// ALTERNATIVE SEGMENTS
//


// add or update alternative segment
// referenced by ptr to start of alternative segment group in regex i.e. regex 1st char or after a (
// if new item, set regexBaseP = regexP, otherwise first copy former regexNextP as regexBaseP only if regexNextP has changed
// parm
//  regexP              ptr reference segment
//  regexNextP          ptr next alternative segment
//  setAltSegmChanged   set altSegmChanged if updated
// ret
//  1 ok, 0 fail, set retSts

UInt8 AltSegmAdd(const char* regexP, const char* regexNextP)
{
    ALTSEGM* asP;
    UInt16 t;

#if MXREGEX_DEBUG
    snprintf(buf, sizeof(buf), "- AltSegmAdd ref regexP: %s regexNextP: %s\r\n", regexP, regexNextP);
    OutputDebugStringA((LPCSTR)buf);
#endif

    for (t = 0; t < m.altSegmNum; t++)
    {
        asP = &m.altSegm[t];

        if (asP->regexP == regexP)                          // if position already present
        {
            if (asP->regexNextP != regexNextP)              // if update value
            {
                asP->regexBaseP = asP->regexNextP;
                asP->regexNextP = regexNextP;

                m.altSegmChanged = 1;
#if MXREGEX_DEBUG
                snprintf(buf, sizeof(buf), "- update, set altSegmChanged, baseP: %s, nextP: %s\r\n", asP->regexBaseP, asP->regexNextP);
                OutputDebugStringA((LPCSTR)buf);
#endif
            }
            return 1;
        }
    }

    // here add a new location

    if (m.altSegmNum >= MAX_ALTSEG)                         // check ovf
    {
        m.retSts = REGEXSTS_ALTSEGM_OVF;
        return 0;
    }

    asP = &m.altSegm[m.altSegmNum++];
    asP->regexP = regexP;
    asP->regexBaseP = regexP;
    asP->regexNextP = regexNextP;

    m.altSegmChanged = 1;

#if MXREGEX_DEBUG
    snprintf(buf, sizeof(buf), "- add, set altSegmChanged, baseP: %s, nextP: %s\r\n", asP->regexBaseP, asP->regexNextP);
    OutputDebugStringA((LPCSTR)buf);
#endif

    return 1;
}




// get ptr to active alternative segment regexBaseP
// referenced by ptr to start of alternative segment group in regex i.e. regex 1st char or after a (
// parm
//  regexP    ptr alternative segments base
// ret
//  ptr current alternative segment
//  0 no alternative segment found

const char* AltSegmGet(const char* regexP)
{
    ALTSEGM* asP;
    UInt16 t;

    for (t = 0; t < m.altSegmNum; t++)
    {
        asP = &m.altSegm[t];
        if (asP->regexP == regexP)
            return asP->regexBaseP;                         // found
    }

    return 0;                                               // not found
}




// remove all alternative segments AFTER regexBaseP, remove empty
// parm
//  regexP  ptr after which descriptor will be removed  0 -> all

void AltSegmRemoveAt(const char* regexP)
{
    ALTSEGM* asP;
    UInt16 t;
    UInt16 t1;

    t1 = 0;
    for (t = 0; t < m.altSegmNum; t++)
    {
        asP = &m.altSegm[t];
        if (asP->regexP > regexP                        // if to be removed or empty: remove
            || asP->regexP == 0)
        {
#if MXREGEX_DEBUG
            snprintf(buf, sizeof(buf), "- AltSegmRemoveAt %s, set altSegmChanged, entry removed regexP: %s regexBaseP: %s\r\n", regexP, asP->regexP, asP->regexBaseP);
            OutputDebugStringA(buf);
#endif
            continue;
        }
        if (t1 != t)
        {
            m.altSegm[t1].regexP = asP->regexP;
            m.altSegm[t1].regexBaseP = asP->regexBaseP;
            m.altSegm[t1].regexNextP = asP->regexNextP;
        }
        t1++;
    }

    if (t1 != t)
    {
        m.altSegmNum = t1;                                  // adj if changed
        m.altSegmChanged = 1;
    }
#if MXREGEX_DEBUG
    else
    {
        snprintf(buf, sizeof(buf), "- AltSegmRemoveAt %s, no changes\r\n", regexP);
        OutputDebugStringA(buf);
    }
#endif

    return;
}




// search for alternative segment
//
// parm
//  segmentP
//  mode 0: search for ')', 1: search for '|' or ')',
// ret
//  0 no further alt segment or error
//  1 found ')'
//  2 found '|'
//  segmentP->regexParseP updated to char after closing element

UInt8 AltSegmSearch(SEGMENT* segmentP, UInt8 mode)
{
    UInt16 t1;

    // search for closing bracket (if nested)

    t1 = 0;

    while (1)
    {
        if (*segmentP->regexParseP == '\0')             // if \0 reached, search end
        {
            if (t1 > 0)                                 // if unbalanced brachets: fail
                m.retSts = REGEXSTS_SYNTAX;
            break;
        }

        if (*segmentP->regexParseP == '\\')             // if esc, skip
        {
            segmentP->regexParseP++;
            if (*segmentP->regexParseP++ == '\0')       // \0 after ESC, fail
            {
                m.retSts = REGEXSTS_SYNTAX;
                break;
            }
            continue;
        }

        if (mode == 1)
        {
            if (*segmentP->regexParseP == '|' && t1 == 0)   // if found | start alternative block on current nesting level
            {
                segmentP->regexParseP++;                                    // resume regex from char after |
                return 2;                                   // got |
            }
        }

        if (*segmentP->regexParseP == '(')              // se ( incerase nesting
            t1++;

        else if (*segmentP->regexParseP == ')')         // if ) decrease nesting, will match only on nested regex
        {
            if (t1-- == 0)                              // if at current level, it's the closing one
            {
                segmentP->regexParseP++;
                return 1;                               // but parse is ok
            }
        }

        segmentP->regexParseP++;

    }//while 1

    return 0;                                           // element not found

}




// update altSegm for next iteration
//
// - search for righmost altSegm nextP
// - if found, move to next element
// parm
//  parseP  ptr from where to seach altSegm
// ret
//  1 elements updated, require reparse
//  0 no change, reparse not necessary

UInt8 AltSegmIterate(const char* regexP)
{
    ALTSEGM* asP;
    UInt16 t;
    UInt16 t1;

    t = MAX_ALTSEG;                                                 // init as not present

    for (t1 = 0; t1 < m.altSegmNum; t1++)
    {
        asP = &m.altSegm[t1];
        if (asP->regexP >= regexP)                                  // if valid range
        {
            if (asP->regexNextP > asP->regexBaseP)                  // if valid alt segment
            {
                if (t < MAX_ALTSEG)                                 // if not first one
                {
                    if (asP->regexNextP > m.altSegm[t].regexNextP)  // compare and tag if at the right
                        t = t1;
                }
                else
                    t = t1;                                         // first occurrence, just tag
            }
        }
    }

    if (t < MAX_ALTSEG)                                             // if alt segment found, update parsepoint
    {
        asP = &m.altSegm[t];

#if MXREGEX_DEBUG
        snprintf(buf, sizeof(buf), "- AltSegmIterate update regexP: %s, regexBaseP: %s <- regexNextP: %s\r\n", asP->regexP, asP->regexBaseP, asP->regexNextP);
        OutputDebugStringA((LPCSTR)buf);
#endif
        asP->regexBaseP = asP->regexNextP;
        AltSegmRemoveAt(asP->regexBaseP);                           // remove alt segments after this point

        return 1;
    }

#if MXREGEX_DEBUG
    snprintf(buf, sizeof(buf), "- AltSegmIterate no change\r\n");
    OutputDebugStringA((LPCSTR)buf);
#endif

    return 0;                                                       // no change
}







// init segment vars
// parm
//  recurseNum  nesting level
//  strP        ptr base str
//  regexP      ptr base regex
//  mode        regex flags
//  isCap       capturing segment
// ret
//  REGEXSTS_OK: ok
//  else fatal error

REGEX_STS SegmentInit(const UInt16 recurseNum, const char* strP, const char* regexP, const UInt16 mode, UInt8 isCap)
{
    SEGMENT* segmentP;

    if (recurseNum >= MAX_RECURSE)              // recurse overfflow, fail
        return REGEXSTS_RECURSE_OVF;

    // init dati branch

    segmentP = &m.segment[recurseNum];

    segmentP->strP = strP;                      // ptr str base
    segmentP->strParseP = strP;                 // ptr str in parsing
    segmentP->regexP = regexP;                  // ptr regex base
    segmentP->regexParseP = regexP;             // ptr regex in parsing
    segmentP->strCapP = strP;                   // ptr capture str
    segmentP->mode = mode;                      // segment mode
    segmentP->isCap = isCap;                    // is a capture

    segmentP->isCI = (mode & REGEXMODE_CASE_INSENSITIVE) ? 1 : 0;       // set initial case insensitive flag

    return REGEXSTS_OK;
}




// save caps if is capture
// ret
// 1 ok, 0 fail (see retSts)
// NOTE caps[0] is reserved to match, start from caps[1]

UInt8 CapsSave(SEGMENT* segmentP)
{
    CAPS* capsP;
    UInt16 t;

    if (segmentP->isCap)                                    // IF IS CAPTURE, SAVE TO CAPS
    {
        capsP = 0;
        for (t = 1; t < m.capsNum; t++)                     // check if parsepoint already present (one bracket pair may add only one caps)
        {
            capsP = &m.caps[t];
            if (capsP->regexP == segmentP->regexP           // is caps already present or null: overwrite
                || capsP->regexP == 0)
                goto BR_SAVE;
        }

        if (m.capsNum >= MAX_CAPS)                      	// check for ovf, add new caps
        {
            m.retSts = REGEXSTS_CAPS_OVS;
            return 0;
        }

        capsP = &m.caps[m.capsNum++];                       // add new caps

    BR_SAVE:

        capsP->regexP = segmentP->regexP;
        capsP->strP = segmentP->strCapP;
        capsP->len = segmentP->strParseP - segmentP->strCapP;

#if MXREGEX_DEBUG
        snprintf(buf, sizeof(buf), "- CapsSave %d regexP: %s strP: %s len: %d \r\n", t, capsP->regexP, capsP->strP, capsP->len);
        OutputDebugStringA((LPCSTR)buf);
#endif
    }

    return 1;
}



// remove all caps after regexP (included)

void CapsRemove(const char* regexP)
{
    CAPS* cP;
    UInt16 t;

    for (t = 0; t < m.capsNum; t++)
    {
        cP = &m.caps[t];
        if (cP->regexP >= regexP)
        {
#if MXREGEX_DEBUG
            snprintf(buf, sizeof(buf), "- CapsRemove %d, regexP: %s removed at: %s\r\n", t, regexP, cP->regexP);
            OutputDebugStringA((LPCSTR)buf);
#endif
            cP->regexP = 0;
        }
    }

    return;
}






// recursive regex parser
// possible captures will be saved to caps, incrementing capsNum
// parm:
//  recurseNum: recursion depth; bound check in MxRegex_init
// ret
//  1 regex match
//  0 fail, check retSts for fatal errors

UInt8 MxRegex_(UInt16 recurseNum)
{
    SEGMENT* segmentP;
    BACKTRACK* backtrackP;

    static UInt16 t;                                                // locals not used in recursion, can be static
    static const char* cP;

    segmentP = &m.segment[recurseNum];

    segmentP->segmNumOcc = 0;
    segmentP->parseFailed = 0;
    segmentP->isEnoughOcc = 0;
    segmentP->strCharAcquired = 0;                                  // clear flag char acquired


    if ((cP = AltSegmGet(segmentP->regexP)))                        // get current alternative segment (0 = not found).
    {
        segmentP->regexParseP = cP;                                 // if exists, set parser
#if MXREGEX_DEBUG
        snprintf(buf, sizeof(buf), "- initial AltSegmGet regexP: %s, regexParseP: %s\r\n", segmentP->regexP, segmentP->regexParseP);
        OutputDebugStringA((LPCSTR)buf);
#endif
    }

    while (1)
    {

        segmentP->atomNumOcc = 0;                                   // restart counting atom occurrencies
        backtrackP = 0;                                             // no backtrack. Will be updated on regex CHAR or METACLASS

#if MXREGEX_DEBUG
        snprintf(buf, sizeof(buf), "%4d %2d %-50s %-70s %-50s %-70s\r\n",
            ++step,
            recurseNum,
            segmentP->strParseP,
            segmentP->regexParseP,
            segmentP->strP,
            segmentP->regexP
        );
        OutputDebugStringA((LPCSTR)buf);
        if (step == 1)
        {
            Nop();                                                  // breakpoint at step n
        }
#endif

        if (m.iterateCnt++ >= MAX_ITERATE)                          // watchdog
        {
            m.retSts = REGEXSTS_MAXITERATE_OVF;
            return 0;
        }

        if ((m.retSts = GetRegexAtom(segmentP->regexParseP, segmentP->isCI)) != REGEXSTS_OK)    // get next atom
            return 0;                                                           // if errors reported, fail

        segmentP->regexParseP = m.atom.endP;                        // move regex parser AFTER atom



    BR_MULTIPLE_OCC:                                                // *** entrypoint multiple occurrences for same atom

        switch (m.atom.type)
        {


        case ATOMTYPE_EOS:                                          // end of regex \0 EOS

#if MXREGEX_DEBUG
            snprintf(buf, sizeof(buf), "- lev = %d EOS\r\n", recurseNum);
            OutputDebugStringA((LPCSTR)buf);
#endif

            if (recurseNum > 0)                                     // if nested, missing ), fatal
            {
                m.retSts = REGEXSTS_SYNTAX;
                return 0;
            }
            return 1;                                               // REGEX MATCH, complete



        case ATOMTYPE_PIPE:                                         // | alternative segment, here only if there was a match

#if MXREGEX_DEBUG
            snprintf(buf, sizeof(buf), "- lev = %d PIPE\r\n", recurseNum);
            OutputDebugStringA((LPCSTR)buf);
#endif

            if (recurseNum == 0)                                    // if NOT nested
            {
                segmentP->regexParseP--;                            // remove | from result
                return 1;                                           // REGEX MATCH, complete
            }

            // here recurseNum > 0, it's a nested alternative segment i.e. between (..):
            // we save the the pointer where to resume regex parsing if same segment is re-evaluated.

            if (!CapsSave(segmentP))                                // save caps if is capture
                return 0;

            // save starting point for next iteration

#if MXREGEX_DEBUG
            snprintf(buf, sizeof(buf), "- Pipe MATCH: lev = %d, set alternative segment to %s\r\n", recurseNum, segmentP->regexParseP);
            OutputDebugStringA((LPCSTR)buf);
#endif

            AltSegmAdd(segmentP->regexP, segmentP->regexParseP);    // save next segment, if follows

            if (AltSegmSearch(segmentP, 0))                         // search for closing bracket, if found:
            {
                if (Atom_ParseQtf(segmentP->regexParseP, &t))       // if valid quantifier: save and skip it
                    segmentP->regexParseP += t;
                goto BR_BRACKETCLOSE;                               // handle as bracket close with quantifier
            }

            // no closing bracket, fatal

            m.retSts = REGEXSTS_SYNTAX;
            return 0;



        case ATOMTYPE_METACLASS:                                    // charset

            if (segmentP->isCI)                                     // fix 1.04
                t = Atom_charInCharset(&m.atom.charset, Upper(*segmentP->strParseP)) ? 1 : 0;  // t=1 if char in charset. NOTE if str at EOS \0, fail
            else
                t = Atom_charInCharset(&m.atom.charset, *segmentP->strParseP) ? 1 : 0;  // t=1 if char in charset. NOTE if str at EOS \0, fail
            goto BR_CHECK_MATCH_ATOM;



        case ATOMTYPE_CHAR:                                         // simple char

            if (segmentP->isCI)                                      // handle case insensitive, t=1 if char match
                t = (Upper(*segmentP->strParseP) == Upper(m.atom.c)) ? 1 : 0;
            else
                t = (*segmentP->strParseP == m.atom.c) ? 1 : 0;

        BR_CHECK_MATCH_ATOM:                                        // *** entrypoint char/charclass match test

            if (m.atom.minOcc < m.atom.maxOcc)                       // if potential backtrack, get descriptor
            {
                backtrackP = BacktrackGet(segmentP->regexParseP);    // GET BACKTRACK descriptor (should always be present)

                if (backtrackP != 0)
                    if (backtrackP->maxOcc == 0)                     // if should fail anyway
                        t = 0;                                       // invalidate match
            }

            if (t == 0)
            {
                //
                // HERE WE HAVE A CHAR/CHARSET NO MATCH CONDITION, THERE COULD BE ALTERNATIVE SEGMENTS
                //

                // ADJ BACKTRACK FOR POSSIBLE NEXT ITERATION

                if (m.atom.minOcc < segmentP->atomNumOcc)           // if there could have been less occurrencies
                {
                    if (backtrackP != 0)                            // if backtrack present (should always be)
                    {
                        backtrackP->maxOcc = segmentP->atomNumOcc;  // update max occurrencies for next round (if needed)
#if MXREGEX_DEBUG
                        snprintf(buf, sizeof(buf), "- backtrack atom set backtrack.maxOcc = %d\r\n", backtrackP->maxOcc);
                        OutputDebugStringA((LPCSTR)buf);
#endif
                    }
                }

                if (segmentP->atomNumOcc >= m.atom.minOcc)          // IF MINIMUM OCCURRENCE SATISFIED, IT IS A MATCH ANYWAY
                    break;                                          // move to next atom on same str char


                // here regex failed search on current segment, check if follows an alternative segment |
                // if present: restart regex parse from 1st atom of alternative segment,
                // otherwhise: im possible, skip to ) for current nidification and evaluate quantifier for further processing

            BR_SEGMENT_MATCH_FAIL:                                  // *** entrypoint segment match fail, check for alternate segments

#if MXREGEX_DEBUG
                snprintf(buf, sizeof(buf), "SEGMENT MATCH FAIL lev = %d\r\n", recurseNum);
                OutputDebugStringA((LPCSTR)buf);
#endif

                // check for alternative segments

                t = AltSegmSearch(segmentP, 1);                     // check for alt segments or closing bracket

                if (t == 2)                                         // if |
                {
                    AltSegmAdd(segmentP->regexP, segmentP->regexParseP);   // save next segment
                    AltSegmIterate(segmentP->regexP);
                    segmentP->strParseP = segmentP->strP;           // restart str parsing for this segment
                    segmentP->strCapP = segmentP->strP;
                    goto BR_RETRY;
                }

                if (t == 1)                                         // )
                {
                    segmentP->parseFailed = 1;                      // report segment fail
                    if (Atom_ParseQtf(segmentP->regexParseP, &t))   // if valid quantifier: save and skip it
                        segmentP->regexParseP += t;
                    goto BR_BRACKETCLOSE;                           // handle bracket close with quantifier
                }

                // here there is no alternate segment nor closing bracket

                if (m.retSts != REGEXSTS_OK)                        // check for fatal error condition
                    return 0;

                //
                // HERE WE HAVE A REGEX NO MATCH CONDITION ON CURRENT SEGMENT
                //

                if (!segmentP->isEnoughOcc)                     // if not enough occurrences collected check for backtrack and altSegm
                {
                    if (BacktrackIterate(segmentP->regexP))
                        goto BR_RETRY;
                }

                if (recurseNum > 0)                             // if nested, remove alt segm descriptor for this segment and return no match
                {
                    return 0;
                }

                //
                // here is the base segment (i.e. recurseNum == 0)
                //

                if (*segmentP->strP == '\0')                    // if str EOS reached, REGEX NO MATCH
                {
#if MXREGEX_DEBUG
                    snprintf(buf, sizeof(buf), "- str EOS REACHED %d\r\n", recurseNum);
                    OutputDebugStringA((LPCSTR)buf);
#endif
                    return 0;
                }

                if (m.altSegmChanged)                           // if still alternate segments pending
                {
#if MXREGEX_DEBUG
                    snprintf(buf, sizeof(buf), "- lev = %d altSegmChanged true, clear altSegmChanged, restart\r\n", recurseNum);
                    OutputDebugStringA((LPCSTR)buf);
#endif

                    if (AltSegmIterate(segmentP->regexP))
                    {
                        goto BR_RETRY;
                    }

                    m.altSegmChanged = 0;
                    m.capsNum = 1;                              // clear all caps  (keep caps[0] as matched string, if it's a match)
                    goto BR_RETRY;                              // repeat iteration
                }


                // HERE WE CAN MOVE NO NEXT STR CHAR

                segmentP->strP++;                               // move to next char
                m.backtrackNum = 0;                             // clear all backtrack
                m.altSegmNum = 0;                               // clear all alternate segments
                m.iterateCnt = 0;                               // restart watchdog
                m.capsNum = 1;                                  // clear all caps  (keep caps[0] as matched string, if it's a match)

#if MXREGEX_DEBUG
                snprintf(buf, sizeof(buf), "\r\n\r\n*** MOVE TO NEXT char, clear backtrack, altSegm, capsNum, iterationCnt; strP: %s\r\n", segmentP->strP);
                OutputDebugStringA((LPCSTR)buf);
#endif


            BR_RETRY:                                           // *** entrypoint retry regex on str

#if MXREGEX_DEBUG
                snprintf(buf, sizeof(buf), "\r\n*** RETRY regexP: %s\r\n", segmentP->regexP);
                OutputDebugStringA((LPCSTR)buf);
#endif
                //AltSegmRemoveAt(segmentP->regexP);
                if ((cP = AltSegmGet(segmentP->regexP)))                        // get current alternative segment (0 = not found).
                {
                    segmentP->regexParseP = cP;                                 // if exists, set parser
#if MXREGEX_DEBUG
                    snprintf(buf, sizeof(buf), "- retry AltSegmGet regexP: %s, regexParseP: %s\r\n", segmentP->regexP, segmentP->regexParseP);
                    OutputDebugStringA((LPCSTR)buf);
#endif
                }
                else
                    segmentP->regexParseP = segmentP->regexP;   // restart regex parser

                segmentP->strCharAcquired = 0;                  // clear flag char acquired
                segmentP->parseFailed = 0;                      // clear error status for this segment
                segmentP->strParseP = segmentP->strP;           // restart parsing

                CapsRemove(segmentP->regexP);
                break;                                          // continue (or restart) evaluation

            }// if match fail


            //
            // HERE WE HAVE A MATCH CONDITION
            //

            // check if there are multiple occurrences (greedy): if not, move to next str char

            segmentP->atomNumOcc++;                                 // ++nr occurrencies found

            if (backtrackP != 0)                                    // if backtrack present
            {
                if (m.atom.maxOcc > backtrackP->maxOcc)
                {
                    m.atom.maxOcc = backtrackP->maxOcc;             // adj maxOcc
#if MXREGEX_DEBUG
                    snprintf(buf, sizeof(buf), "- backtrack match atom maxOcc = %d\r\n", backtrackP->maxOcc);
                    OutputDebugStringA((LPCSTR)buf);
#endif
                }
            }

            segmentP->strParseP++;                                  // move to next str char (THIS IS THE ONLY PLACE WHERE IT HAPPENS)
            segmentP->strCharAcquired = 1;                          // set flag

            if (segmentP->atomNumOcc < m.atom.maxOcc)               // if max occurrencies NOT reached, repeat test on next str char
                goto BR_MULTIPLE_OCC;

            //
            // HERE QUANTIFIER IS FULLFILLED
            //

            // ADJ BACKTRACK FOR NEXT ITERATION

            if (m.atom.minOcc < segmentP->atomNumOcc)               // if there could have been less occurrencies
            {
                if ((backtrackP = BacktrackGet(segmentP->regexParseP)))     // if backtrack present (should always be)
                {
                    backtrackP->maxOcc = segmentP->atomNumOcc;      // update max occurrencies for next round (if needed)
#if MXREGEX_DEBUG
                    snprintf(buf, sizeof(buf), "- backtrack atom set backtrack.maxOcc = %d\r\n", backtrackP->maxOcc);
                    OutputDebugStringA((LPCSTR)buf);
#endif
                }
            }

            break;                                                  // move to next atom



        case ATOMTYPE_ANCHOR:                                       // anchor ^ $ \b ..

            if (m.atom.c == '^')                                    // ^ match only from str begin
            {
                if (segmentP->strParseP == m.strOrigP)              // if 1st char: ok
                    break;

                if (m.isMultiLine)                                  // if multiline: ok also if preceding char was \r or \n
                    if (segmentP->strParseP[-1] == '\r' || segmentP->strParseP[-1] == '\n')
                        break;

                goto BR_SEGMENT_MATCH_FAIL;                         // fail
            }

            if (m.atom.c == '$')                                    // $ end of string \0 EOS
            {
                // check char following current one

                if (m.isMultiLine)                                  // multiline \0 \n \r  if no match, move to next str char
                {
                    if (*segmentP->strParseP != '\0'
                        && *segmentP->strParseP != '\r'
                        && *segmentP->strParseP != '\n')
                        goto BR_SEGMENT_MATCH_FAIL;
                }
                else
                {
                    if (*segmentP->strParseP != '\0')               // singleline \0
                        goto BR_SEGMENT_MATCH_FAIL;
                }
                break;
            }

            if (m.atom.c == 'b')                                    // \b word boundary   transition \W->\w or \w->\W
            {
                if (segmentP->strParseP == segmentP->strP)          // if str first char, ok
                    break;

                if (IsWord(segmentP->strParseP[-1]) && !IsWord(segmentP->strParseP[0]))     // if transition \w->\W from preceding, ok
                    break;

                if (!IsWord(segmentP->strParseP[-1]) && IsWord(segmentP->strParseP[0]))      // if transition \W->\w, ok
                    break;

                goto BR_SEGMENT_MATCH_FAIL;                         // no match, move to next str char

            }

            if (m.atom.c == 'B')                                    // \B non-word boundary
            {
                if (segmentP->strParseP == segmentP->strP)          // if str first char, fail
                    goto BR_SEGMENT_MATCH_FAIL;

                if (IsWord(segmentP->strParseP[-1]) && IsWord(segmentP->strParseP[0]))       // if no transition \w->\W from preceding, ok
                    break;

                if (!IsWord(segmentP->strParseP[-1]) && !IsWord(segmentP->strParseP[0]))     // if no transizione \W->\w, ok
                    break;

                goto BR_SEGMENT_MATCH_FAIL;                         // no match, move to next str char
            }

            // unhandled anchor, ignore and treat as a match

            break;



        case ATOMTYPE_BRACKETOPEN:                                  // (  inizio sub-regex

#if MXREGEX_DEBUG
            snprintf(buf, sizeof(buf), "- lev = %d BRACKET OPEN\r\n", recurseNum);
            OutputDebugStringA((LPCSTR)buf);
#endif

            t = segmentP->regexParseP[0] == '?' && segmentP->regexParseP[1] == ':';      // check if non-capture
            if (t)
                segmentP->regexParseP += 2;                          // skip mode

            if ((m.retSts = SegmentInit(recurseNum + 1, segmentP->strParseP, segmentP->regexParseP, segmentP->mode, t == 0)) != REGEXSTS_OK)
                return 0;                                           // init parser, exit on error

            if (!MxRegex_(recurseNum + 1))                          // INVOKE NESTED REGEX
            {
                if (m.retSts != REGEXSTS_OK)                        // if any fatal error
                {
                    segmentP->regexParseP = m.segment[recurseNum + 1].regexParseP;  // save error position
                    return 0;                                                       // and fail
                }

                segmentP->regexParseP = m.segment[recurseNum + 1].regexParseP;
                goto BR_SEGMENT_MATCH_FAIL;
            }

            // sub reges success, adj str and regex parsing point, continue eval from there

            segmentP->strParseP = m.segment[recurseNum + 1].strParseP;
            segmentP->regexParseP = m.segment[recurseNum + 1].regexParseP;

            break;



        case ATOMTYPE_BRACKETCLOSE:                                 // ) end of nested regex, here only if there was a match

        BR_BRACKETCLOSE:                                            // *** entrypoint parse bracket close after pipe |  (match or non match)

#if MXREGEX_DEBUG
            snprintf(buf, sizeof(buf), "- lev = %d BRACKET CLOSE\r\n", recurseNum);
            OutputDebugStringA((LPCSTR)buf);
#endif

            if (recurseNum == 0)                                    // if not nested, not allowed
            {
                m.retSts = REGEXSTS_SYNTAX;
                return 0;
            }

            if (m.atom.minOcc < m.atom.maxOcc)                      // if it's a possible trackback position, add if necessary
                BacktrackAdd(segmentP->regexParseP);

            backtrackP = BacktrackGet(segmentP->regexParseP);       // GET BACKTRACK descriptor (should always be present)

            if (m.atom.minOcc < m.atom.maxOcc)                      // if potential backtrack, get descriptor
            {
                if (backtrackP != 0)
                    if (backtrackP->maxOcc == 0)                    // if should fail anyway..
                        return 0;                                   // return no match
            }

            if (segmentP->parseFailed)                              // if no match
            {
                if (m.atom.minOcc <= segmentP->segmNumOcc)          // if ENOUGH occurrencies according to quantifier, success
                {
                    if (backtrackP != 0)
                    {
                        backtrackP->maxOcc = segmentP->segmNumOcc;  // update max occurrencies for next round (if needed)
#if MXREGEX_DEBUG
                        snprintf(buf, sizeof(buf), "- backtrack bracket close set backtrack.maxOcc = %d\r\n", backtrackP->maxOcc);
                        OutputDebugStringA((LPCSTR)buf);
#endif
                    }
#if MXREGEX_DEBUG
                    snprintf(buf, sizeof(buf), "- bracket close segment MATCH enough occurrences minOcc = %d restore strParseP\r\n", m.atom.minOcc);
                    OutputDebugStringA((LPCSTR)buf);
#endif
                    // restore possible parsed str chars if last iteration failed
                    segmentP->strParseP = segmentP->strCapP;
                    return 1;
                }


#if MXREGEX_DEBUG
                snprintf(buf, sizeof(buf), "- bracket close FAIL regexP: %s\r\n", segmentP->regexP);
                OutputDebugStringA((LPCSTR)buf);
#endif

                return 0;                                           // otherwise fail
            }


            // HERE WE HAVE A SEGMENT MATCH.
            // If capture, save result (only last one). See also PIPE


            if (!CapsSave(segmentP))                                // fail to save: fatal
                return 0;

            segmentP->segmNumOcc++;                                 // INCREMENT NR OCCURRENCIES

            if (backtrackP != 0)                                    // if backtrack present
            {
                if (m.atom.maxOcc > backtrackP->maxOcc)
                {
                    m.atom.maxOcc = backtrackP->maxOcc;             // adj maxOcc
#if MXREGEX_DEBUG
                    snprintf(buf, sizeof(buf), "- backtrack bracket close set atom.maxOcc = %d\r\n", backtrackP->maxOcc);
                    OutputDebugStringA((LPCSTR)buf);
#endif
                }
            }

            // check quantifier

            if (segmentP->segmNumOcc >= m.atom.maxOcc)               // if reached max occurrencies according to quantifier: success
                return 1;

            if (!segmentP->strCharAcquired)                         // if minOcc == 0 and no char captured, cannot be greedy
                return 1;

            segmentP->strCharAcquired = 0;                          // retrig flag char acquired

            if (segmentP->segmNumOcc >= m.atom.minOcc)              // if got minocc, enough occurrences for segment match, no backtrack
            {
                segmentP->strP = segmentP->strParseP;               // update base str: parsed chars are definitive
                segmentP->isEnoughOcc = 1;
#if MXREGEX_DEBUG
                snprintf(buf, sizeof(buf), "- lev = %d set isEnoughOcc\r\n", recurseNum);
                OutputDebugStringA((LPCSTR)buf);
#endif
            }

            // quantifier requires more iteration, restart parser and set potential new caps start

            segmentP->strCapP = segmentP->strP;                     // update caps base if next iteration is a match
            segmentP->regexParseP = segmentP->regexP;
            break;

        }// switch

    }// while 1 (passa ad atom successivo)

    m.retSts = REGEXSTS_SYNTAX;                                     // should never reach this point, fail
    return 0;

}// MxRegex_





// clear regex parser descriptors

void ClearDescriptors()
{
#if MXREGEX_DEBUG

    UInt16 t;

    // not necessary, just useful for debugging

    for (t = 0; t < MAX_ALTSEG; t++)            // clear alternative segment info
    {
        m.altSegm[t].regexP = 0;
        m.altSegm[t].regexBaseP = 0;
        m.altSegm[t].regexNextP = 0;
    }

    for (t = 0; t < MAX_CAPS; t++)              // clear caps info
    {
        m.caps[t].len = 0;
        m.caps[t].strP = 0;
    }

    for (t = 0; t < MAX_BACKTRACK; t++)         // clear backtrack info
    {
        m.backtrack[t].maxOcc = 0;
        m.backtrack[t].minOcc = 0;
        m.backtrack[t].regexParseP = 0;
    }

#endif

    m.altSegmNum = 0;
    m.capsNum = 0;
    m.backtrackNum = 0;

    return;
}






//
// PUBLIC METHODS
//


// Init regex machine
// MUST be called once at startup

void MxRegex_init()
{

#if !CONST_CHARSET

    // prepare predefined charset
    // used for code optimization in case of complex sets
    // could be hardcoded in order to save RAM (160 bytes)

    Atom_charsetResetAll();                                         // . (include \r\n)
    Atom_charsetInvert();
    Atom_charsetExport(&m.charset_dot);

    Atom_charsetResetAll();                                         // \w
    Atom_charsetAddStr(C_WORD_CHARSET_STR);
    Atom_charsetExport(&m.charset_word);

    Atom_charsetResetAll();                                         // \d
    Atom_charsetAddStr(C_DIGIT_CHARSET_STR);
    Atom_charsetExport(&m.charset_digit);

    Atom_charsetResetAll();                                         // \s
    Atom_charsetAddStr(C_WHITESPACE_CHARSET_STR);
    Atom_charsetExport(&m.charset_whitespace);

#endif

    return;
}







// Regex
//
// parm
//  regexP      ptr to regex pattern string (\0 terminated)
//  strP        ptr to input string (\0 terminated)
//  mode        bitfld REGEX_MODE (currently available: case insensitive)
// ret
//  0           regex fail or errors detected (you may check m.retSts, m.retRegexOfs for error description)
//  n           regex match, number of captures. caps[0] is always the match, caps[1..maxCaps-1] are the captures

UInt8 MxRegex(const char* regexP, const char* strP, const UInt16 mode)
{
    UInt16 t;
    UInt16 t1;
    CAPS* cP;

    // initialize regex parser
    ClearDescriptors();

    // store mode flags
    m.isMultiLine = (mode & REGEXMODE_MULTILINE) ? 1 : 0;
    m.isSingleLine = (mode & REGEXMODE_SINGLELINE) ? 1 : 0;

    m.retRegexErrOfs = 0;       // clear error position
    m.strOrigP = strP;          // save ptr to original string

    m.altSegmNum = 0;           // clear alternative segments descriptors


    // init regex as segment[0], on base str. Here is always non-capture
    if ((m.retSts = SegmentInit(0, strP, regexP, mode, 0)) == REGEXSTS_OK)
    {
        m.capsNum = 1;                                      // will populate caps starting at [1]
        m.altSegmChanged = 0;                               // clear alternate segments changed flag
        m.iterateCnt = 0;                                   // init watchdog

        // invoke first regex

        if (MxRegex_(0))                                    // if success, set caps[0] to matched string
        {
            cP = &m.caps[0];
            cP->strP = m.segment[0].strP;
            cP->len = m.segment[0].strParseP - m.segment[0].strP;
            cP->regexP = m.segment[0].regexP;

            // remove possible empty capsnum

            t1 = 1;
            for (t = 1; t < m.capsNum; t++)
            {
                cP = &m.caps[t];
                if (cP->regexP == 0)                        // skip empty
                    continue;

                if (t1 != t)                                // if need to move
                {
                    m.caps[t1].strP = cP->strP;
                    m.caps[t1].regexP = cP->regexP;
                    m.caps[t1].len = cP->len;
                }
                t1++;
            }
            m.capsNum = t1;
            return (UInt8)m.capsNum;                        // MATCH
        }
    }

    // no match / error

    m.retRegexErrOfs = m.segment[0].regexParseP - m.segment[0].regexP;    // on fail set error offset returned from Regex_
    m.capsNum = 0;                                          // reset capsnum

    return 0;                                               // FAIL

}



// get regex capture results (match)
// parm:
//  capsNum: capture position, 0 is the whole match, 1..n are the capturing brackets
//  retStr: RET pointer to capture within parsed string (i.e. no \0 termination)
//  retLen: RET capture length
// ret:
//  1: success, 0: capture not available

UInt8 MxRegex_getCaps(const UInt16 capsNum, char** retStr, UInt16* retLen)
{
    if (capsNum >= m.capsNum)
    {
        *retStr = 0;
        *retLen = 0;
        return 0;
    }

    *retStr = (char*)(m.caps[capsNum].strP);
    *retLen = m.caps[capsNum].len;

    return 1;
}



// get regex public vars
// usually for debug only
// ret:
//  mP:     ptr to MXREGEX_M m
// useful elements
//  mP->m.retSts        if regex fail, get reason (REGEXSTS_OK: simply no match)
//  mp->m.retRegexOfs   if not REGEXSTS_OK, error position in regex
//  mp->m.capsNum       nr of captures available

const MXREGEX_M* MxRegex_getData()
{
    return &m;
}









// TEST ONLY

#if MXREGEX_DEBUG

int main()
{

    UInt8 b;
    UInt16 t;

    step = 0;
    snprintf(buf, sizeof(buf), "\r\n\n\n");
    OutputDebugStringA((LPCSTR)buf);

    MxRegex_init();

    // TEST
    b = MxRegex("^SPK((?:\\s*[+-][VAP])+)$", "spk -v+a", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE | REGEXMODE_MULTILINE); // (4,7)
    //b = MxRegex("^123$|^456", "asd\n123\raaa", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE | REGEXMODE_MULTILINE); // (4,7)
    //b = MxRegex("^([a-zA-Z0-9._%-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,6})$", "address.ext@gmail.com", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE); // (0,21)
    //b = MxRegex("^[\\w-.]+(\\.\\w{2,3})$", "apn.vodafone.it", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE); // (0,15)(12,15)
    //b = MxRegex("(a.*z|b.*y)*.*", "azbazbyc", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);  // (0,8) [ (0,5) ]
    //b = MxRegex( "a(b)|c(d)|a(e)f", "aef",REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);     // (0,3)(?,?)(?,?)(1,2)
    //b = MxRegex("(a|b)*c", "abc", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);   // (0,3)(?,?)(?,?)(1,2)
    //b = MxRegex("(a|b)*c|(a|ab)*c", "xc", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);       // (1,2)
    //b = MxRegex("(.a|.b).*|.*(.a|.b)", "xa", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);       // (0,2)(0,2)
    //b = MxRegex("a+b+c", "aabbabc", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);       // (4,7)
    //b = MxRegex("([abc])*bcd", "abcd", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);      // (0,4)(0,1)
    //b = MxRegex("(...|aa)*a", "aa", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);         // (0,1) | (1,2)
    //b = MxRegex("(a*)(a|aa)", "aaaa", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);      // (0,4)(0,3)(3,4)
    //b = MxRegex("(a*)(b{0,1})(b{1,})b{3}", "aaabbbbbbb", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);      // (0,10)(0,3)(3,4)(4,7)
    //b = MxRegex("((foo)|(bar))!bas", "foo!bar!bas", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);      // (4,11)(4,7)(?,?)(4,7) ***
    //b = MxRegex("^(([^!]+!)?([^!]+)|.+!([^!]+!)([^!]+))$", "foo!bar!bas", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);      // (0,11)(0,11)(?,?)(?,?)(4,8)(8,11)
    //b = MxRegex("^([^!]+!)?([^!]+)$|^.+!([^!]+!)([^!]+)$", "foo!bar!bas", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);      // (0,11)(?,?)(?,?)(4,8)(8,11)
    //b = MxRegex("(.?)*", "x", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);      // (0,1)(1,1)
    //b = MxRegex("(aba|ab|a)(aba|ab|a)(aba|ab|a)", "ababa", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);      // (0,5)(0,2)(2,4)(4,5)
    //b = MxRegex(".*(b)", "ab", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);      // (0,2)(1,2)
    //b = MxRegex("(.*)c(.*)", "abcde", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);      // (0,5)(0,2)(3,5)
    //b = MxRegex("^(http:\\/\\/www\\.|https:\\/\\/www\\.|http:\\/\\/|https:\\/\\/)?[a-z0-9]+([\\-\\.]{1}[a-z0-9]+)*\\.[a-z]{2,5}(:[0-9]{1,5})?(\\/.*)?$", "https://www.google.com:80", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE); // (0,25)(0,12)(22,25)
    //b = MxRegex("(wee|week)(knights|night)(s*)", "weeknights", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE); // (0-10)(0,3)(3,10)(10,10)
    //b = MxRegex("(weeka|wee)(night|knights)", "weeknights", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);  // (0-10)(0,3)(3,10)
    //b = MxRegex("^\\s*(GET|POST)\\s+(\\S+)\\s+HTTP/(\\d)\\.(\\d)", " \tGET /index.html HTTP/1.0\r\n\r\n", REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);
    //b = MxRegex("[.]","a", REGEXMODE_SINGLELINE);  // fail

    snprintf(buf, sizeof(buf), "\r\nResponse: %s\r\nstatus code: %d \r\ncapsNum: %d\r\n\r", b ? "OK" : "FAIL", m.retSts, m.capsNum);
    OutputDebugStringA((LPCSTR)buf);

    for (t = 0; t < m.capsNum; t++)
    {
        snprintf(buf, sizeof(buf), "caps# %-2d (%2d,%2d) str: %-50s len: %-4d\r\n",
            t,
            m.caps[t].strP - m.strOrigP,
            m.caps[t].strP - m.strOrigP + m.caps[t].len,
            m.caps[t].strP,
            m.caps[t].len);
        OutputDebugStringA((LPCSTR)buf);
        if (t == 0)
            OutputDebugStringA("\r\n");
    }
    OutputDebugStringA("\r\n\n\n");

}

#endif

