# MxRegex

Lightweight non-compiled regex library.

## Description

Small and portable Regular Expression (regex) library written in C.

Mx regex is a trivial non-threadsave regex implementation, suitable for simple queries on embedded systems with small RAM footprint. 
Should not be used for complex pattern matching, i.e. long input strings nor regex with heavy backtrack.

Usage is quite straightforward, e.g.:



    MxRegex_init();     // called once at startup
    MxRegex("^()[a-zA-Z0-9._%-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,6})$"
                ,"address.ext@gmail.com"
                , REGEXMODE_CASE_INSENSITIVE | REGEXMODE_SINGLELINE);
    MxRegex_getCaps(1, &retStr, &retLen);



Developed on Visual Studio 2022, used on STM32 ARM Cortex.

v1.0 features:
- ISO 8859 8-bit charset (i.e. NON unicode)
- occurrences ? * + {a[,[b]]} greedy only
- group (...), non-capturing group (?:...)
- altenative segments a|b
- charset [x] with negation ^x, range a-b
- anchors ^ $ \b \B
- char classes \s \S \d \D \w \W \xHH . (dot)
- multiline, singleline (partial)

