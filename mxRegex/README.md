## MxRegex

#### Description

Mx regex is a trivial, non compiled, non-threadsafe Regular Expression  (regex) library written in C, suitable for simple queries on embedded systems, with small RAM footprint. 

It is not intended for complex pattern matching i.e. long input strings or heavy backtracking.

Allocates about 1K RAM on STM32 ARM, but size may be reduced changing the #defines in MxRegex.h

```c    
#define MAX_RECURSE 5     // max regex nesting i.e. brackets within brackets.
#define MAX_ALTSEG 24     // max number of alternative segments (a|b)
#define MAX_CAPS 12       // max number of capturing brackets, including base caps[0] on regex match.
#define MAX_BACKTRACK 32  // max backtracks
```
<br>&nbsp;
Developed on Visual Studio 2022.
<br>&nbsp;
#### Usage
Usage is quite straightforward:

```c
    // called once at startup
    MxRegex_init();                             

    // perform regex, return nr of captures
    if(MxRegex("^([a-zA-Z0-9._%-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,6})$"
                ,"address.ext@gmail.com"
                , REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE))
    {
        // match
        const char* retStr;
        UInt16 retLen;
        MxRegex_getCaps(0, &retStr, &retLen);   // get match
        MxRegex_getCaps(1, &retStr, &retLen);   // get caps #1
    }
    else
    {
        // No match
    }
```
If regex pattern is hard coded, usually there is no need to check for error conditions. Full data is anyway accessible:
```c
    const MXREGEX_M* mP;

    mP = MxRegex_getData();
    regexStatus = mP->retSts;
```

&nbsp;
#### Features
- ISO 8859-x 8-bit charset
- occurrences: ? * + {a[,[b]]} 
- char classes: . \s \S \d \D \w \W \xHH
- charset: [a] negation [^a], range [a-b]
- anchors: ^ $ \b \B
- group: (...), non-capturing group (?:...)
- altenative segments: a|b
- mode: case sensitive/insensitive
- mode: singleline (for . charclass)

Limitation:
- return on 1st match
- no unicode support
- multiline mode not implemented (for ^ and $ anchor)

To be implemented:
- multiline mode
- non-greedy occurrences

&nbsp;
#### Licence
"MxRegex" is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU General Public License and GNU Lesser General Public License along with this program.  If not, see <http://www.gnu.org/licenses/>

&nbsp;
#### Changelog

##### 1.00
- initial relase