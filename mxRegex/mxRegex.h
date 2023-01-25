/*

This file is part of "MxRegex" library
(C) 2022 Massimo Celeghin 

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





#pragma once

#ifndef MXREGES_H
#define MXREGES_H


#ifdef __cplusplus
extern "C" {
#endif


#define VER "1.00"                              // version



// DEFS

#define MAX_RECURSE 5                           // max regex nesting i.e. brackets within brackets.
#define MAX_ALTSEG 24                           // max number of alternative segments (a|b)
#define MAX_CAPS 12                             // max number of capturing brackets, including base caps[0] on regex match.
#define MAX_BACKTRACK 32                        // max backtracks

#define MAX_ITERATE 1024                        // max iterations on same string (watchdog)

typedef unsigned long UInt32;
typedef unsigned short UInt16;
typedef unsigned char UInt8;






// regex mode, bitfld 8bit

typedef enum
{
    REGEXMODE_NONE = 0x0000,
    REGEXMODE_CASE_INSENSITIVE = 0x0001,        // comparison is case insensitive
    REGEXMODE_MULTILINE = 0x0002,               // anchor ^$ will also match [\r\n]
    REGEXMODE_SINGLELINE = 0x0004               // metaclass . will match ^[0]; if not set will match [^\r\n\0] 

} REGEX_MODE;




// responso valutazione sintassi regex

typedef enum
{
    REGEXSTS_OK = 0,                                        // no errors
    REGEXSTS_SYNTAX,                                        // syntax error
    REGEXSTS_METAGRP_UNKN,                                  // unknown metagroup char (like \w)
    REGEXSTS_QUANTIFIER_ERR,                                // quantifier error
    REGEXSTS_CAPS_OVS,                                      // too many captures
    REGEXSTS_RECURSE_OVF,                                   // nesting overdlow i.e. brackets within brackets
    REGEXSTS_ALTSEGM_OVF,                                   // alternative segments overflow i.e. total nr of active branches (a|b|c..)
    REGEXSTS_BACKTRACK_OVF,                                 // backtrack ovf
    REGEXSTS_MAXITERATE_OVF                                 // too many iteration (watchdog, critical)

} REGEX_STS;





// regex atom type

typedef enum
{
    ATOMTYPE_CHAR = 0,                                      // simple char (also for escaped)
    ATOMTYPE_METACLASS,                                     // metaclass charset like "[abc]"
    ATOMTYPE_BRACKETOPEN,                                   // metachar ( 
    ATOMTYPE_BRACKETCLOSE,                                  // metachar )
    ATOMTYPE_PIPE,                                          // metachar |
    ATOMTYPE_ANCHOR,                                        // anchor 
    ATOMTYPE_EOS                                            // \0

} ATOM_TYPE;





// charset ISO 8859-1   
// 256 bit, bit[ASC(char)] == 1 -> char in set
// note: no big difference if using UInt16 or UInt8 on 16 bit ARM

typedef struct
{
    UInt32  map[8];

} CHARSET;






// regex atom with attributes

typedef struct
{
    ATOM_TYPE type;                             // atom type

    char    c;                                  // char (if simple char)

    UInt16  minOcc;                             // min occurrences
    UInt16  maxOcc;                             // max occurrences
    const char* endP;                           // ptr to char after atom (i.e. after quantifier, if present)

    CHARSET charset;                            // charset
    UInt8 charsetIsNegate : 1;                  // use negated charset [^

    UInt8 charsetAllowNegate : 1;               // charset parser: accept negation [^ 
    UInt8 gotMinus : 1;                         //   char range, got valid '-' 
    char charsetLastChar;                       //   char range, from-char in notation [a-b]

} REGEXATOM;



// segment evaluated by recursive regex. 
// Base segment [0] is the whole string, otherwise is the nested segment between regex brackets 

typedef struct
{
    const char* regexP;                         // ptr base regex
    const char* regexParseP;                    // ptr regex parser 
    const char* strP;                           // ptr base str
    const char* strParseP;                      // ptr str parser 
    const char* strCapP;                        // ptr last capture str, in case of brackets with quantifier

    UInt16  atomNumOcc;                         // atom occurrencies a{n}
    UInt16  segmNumOcc;                         // segment occurrenties (..){n}

    UInt16 mode;                                // regex mode flags
    UInt8 isCI : 1;                             // flag case insensitive
    UInt8 isCap : 1;                            // flag capture segment
    UInt8 parseFailed : 1;                      // flag no match condition (not considering quantifier)
    UInt8 isEnoughOcc : 1;                      // flag got enough occurrences for segment match, no need for backtrack
    UInt8 strCharAcquired : 1;                  // flat at least 1 char acquired from str. Avoid lookup on empty regex like "([ab]*)*a"

} SEGMENT;



// alternative segments [a|b]

typedef struct
{
    const char* regexP;                         // ptr segment (reference)
    const char* regexBaseP;                     // ptr original alternative segment
    const char* regexNextP;                     // ptr to next alternative segment

} ALTSEGM;



// captures

typedef struct
{
    const char* strP;                           // captured string ptr
    const char* regexP;                         // regex segment
    UInt16  len;                                // captured string len

} CAPS;



// backtrack 

#define BACKTRACK_MAXOCC 0xffff                 // no limit to nr of occurrences

typedef struct
{
    const char* regexParseP;                    // backtrack position (ptr to 1st char after atom)
    UInt16 minOcc;                              // current counters
    UInt16 maxOcc;          

} BACKTRACK;




// all static data

typedef struct
{
    SEGMENT segment[MAX_RECURSE];                       // data for recursive regex
    ALTSEGM altSegm[MAX_ALTSEG];                        // alternative segment pointers
    CAPS caps[MAX_CAPS];                                // captures. If success, [0] is the whole match, [1..n] are the capturing bracket
    BACKTRACK backtrack[MAX_BACKTRACK];                 // backtrack handler

    UInt16 altSegmNum;                                  // element counters
    UInt16 capsNum;                                     
    UInt16 backtrackNum;

    REGEXATOM atom;                                     // used by atom parser

    UInt8 isMultiLine : 1;                              // multi line regex mode (see REGEXMODE_MULTILINE)
    UInt8 isSingleLine : 1;                             // single line regex mode 

    REGEX_STS  retSts;                                  // regex status
    UInt16 retRegexErrOfs;                              // in case of error, ptr to regex failed char 
    const char* strOrigP;                               // ptr to original string

    UInt8 altSegmChanged : 1;                           // flag: alternative segments changed, must re-evaluate regex

    UInt16 iterateCnt;                                  // watchdog

    // for code optimization, could be hardcoded to save RAM (128 bytes). See MxRegex_init()

    CHARSET charset_dot;                                // . non singleline, including \r\n
    CHARSET charset_word;                               // \w
    CHARSET charset_digit;                              // \d
    CHARSET charset_whitespace;                         // \s

} MXREGEX_M;




// PUBLIC METHODS

extern void MxRegex_init();                                                         // init charsets, invoked once at startup
extern UInt8 MxRegex(const char* strP, const char* regexP, const UInt16 mode);      // regex
extern UInt8 MxRegex_getCaps(const UInt16 capsNum, char** retStr, UInt16* retLen);  // get captures after regex match
extern const MXREGEX_M* MxRegex_getData();                                          // get all regex data 




#ifdef __cplusplus
}
#endif


#endif
