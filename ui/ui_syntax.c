/*
 * tinyedit - Text editor for AmigaOS
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include "../components/config.h"
#include "ui_syntax.h"

static const char *c_keywords[] =
    {
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "inline", "int", "long", "register", "restrict", "return", "short",
        "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
        "unsigned", "void", "volatile", "while", "_Bool", "_Complex", "_Imaginary",
        "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn", "_Static_assert",
        "_Thread_local", "bool", "true", "false", NULL};

static const char *m68kc_keywords[] =
    {
        "BYTE", "UBYTE", "WORD", "UWORD", "LONG", "ULONG", "FLOAT", "DOUBLE", "BOOL",
        "APTR", "BPTR", "CPTR", "IPTR", "CONST_APTR", "STRPTR", "CONST_STRPTR", "TEXT",
        "RPTR", "BSTR", "SHORT", "USHORT", "COUNT", "UCOUNT", "QUAD", "UQUAD",
        "TagItem", "Tag", "TAG_DONE", "TAG_END", "TAG_IGNORE", "TAG_MORE", "TAG_SKIP", "TAG_USER",
        "MIN", "MAX", "ABS", "CLIP", "SWAP", "LONGBITS", "WORDBITS", "BYTEBITS",
        "BYTEMASK", "TRUE", "FALSE", "GLOBAL", "IMPORT", "BADDR", "MKBADDR",
        NULL};

static const char *cpp_keywords[] =
    {
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "inline", "int", "long", "register", "restrict", "return", "short",
        "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
        "unsigned", "void", "volatile", "while", "_Bool", "_Complex", "_Imaginary",
        "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn", "_Static_assert",
        "_Thread_local",
        "bool", "class", "compl", "const_cast", "delete", "dynamic_cast", "explicit",
        "export", "false", "friend", "mutable", "namespace", "new", "operator",
        "private", "protected", "public", "reinterpret_cast", "static_cast", "template",
        "this", "throw", "true", "try", "typename", "using", "virtual", "wchar_t",
        "override", "final", "nullptr", "constexpr", "decltype", "noexcept",
        "static_assert", "thread_local", NULL};

static const char *x86_keywords[] =
    {
        "mov", "movb", "movw", "movl", "movq", "movsb", "movsw", "movsl", "movsq",
        "movzb", "movzw", "movzl", "movzbl", "movzwl", "movzll", "movabs", "movsx",
        "movsxd", "movzx", "cmove", "cmovne", "cmovz", "cmovnz", "cmova", "cmovae",
        "cmovb", "cmovbe", "cmovg", "cmovge", "cmovl", "cmovle", "cmovo", "cmovno",
        "cmovs", "cmovns", "cmovp", "cmovnp", "sete", "setne", "setz", "setnz", "seta",
        "setae", "setb", "setbe", "setg", "setge", "setl", "setle", "seto", "setno",
        "sets", "setns", "setp", "setnp", "setc", "setnc",
        "push", "pop", "pusha", "popa", "pushad", "popad", "pushf", "popf", "pushfd",
        "popfd", "pushfq", "popfq", "enter", "leave", "lahf", "sahf",
        "add", "sub", "adc", "sbb", "inc", "dec", "neg", "cmp", "mul", "imul", "div",
        "idiv", "aaa", "aas", "aam", "aad", "daa", "das", "cbw", "cwd", "cwde", "cdq",
        "cdqe", "cqo", "bswap", "xadd", "cmpxchg", "cmpxchg8b", "cmpxchg16b", "xchg",
        "and", "or", "xor", "not", "test", "shl", "shr", "sal", "sar", "rol", "ror",
        "rcl", "rcr", "shld", "shrd", "bsf", "bsr", "bt", "btc", "btr", "bts", "bound",
        "arpl",
        "jmp", "je", "jne", "jz", "jnz", "ja", "jae", "jb", "jbe", "jg", "jge", "jl",
        "jle", "jo", "jno", "js", "jns", "jc", "jnc", "jp", "jnp", "jpe", "jpo", "jcxz",
        "jecxz", "jrcxz", "loop", "loope", "loopne", "loopz", "loopnz", "call", "ret",
        "retn", "retf", "iret", "iretd", "iretq", "syscall", "sysret", "sysenter",
        "sysexit", "int", "int3", "into", "hlt",
        "lea", "lds", "les", "lfs", "lgs", "lss", "nop", "pause", "ud2",
        "movs", "movsb", "movsw", "movsd", "movsq", "cmps", "cmpsb", "cmpsw", "cmpsd",
        "cmpsq", "scas", "scasb", "scasw", "scasd", "scasq", "lods", "lodsb", "lodsw",
        "lodsd", "lodsq", "stos", "stosb", "stosw", "stosd", "stosq", "rep", "repe",
        "repz", "repne", "repnz", "ins", "insb", "insw", "insd", "outs", "outsb",
        "outsw", "outsd", "cld", "std", "cli", "sti", "clc", "stc", "cmc", "clts",
        "cpuid", "rdtsc", "rdtscp", "rdmsr", "wrmsr", "rdpmc", "in", "out", "invlpg",
        "wbinvd", "invd", "lock", "xlat", "xlatb", "mfence", "sfence", "lfence",
        "lgdt", "sgdt", "lidt", "sidt", "ltr", "str", "lldt", "sldt", "lmsw", "smsw",
        "fadd", "faddp", "fiadd", "fsub", "fsubp", "fisub", "fsubr", "fsubrp", "fisubr",
        "fmul", "fmulp", "fimul", "fdiv", "fdivp", "fidiv", "fdivr", "fdivrp", "fidivr",
        "fsqrt", "fabs", "fchs", "fscale", "fxch", "fld", "fst", "fstp", "fild", "fist",
        "fistp", "fisttp", "fbld", "fbstp", "ftst", "fcom", "fcomp", "fcompp", "fucom",
        "fucomp", "fucompp", "ficom", "ficomp", "fldcw", "fstcw", "fnstcw", "fldenv",
        "fstenv", "fnstenv", "fsave", "fnsave", "frstor", "fstsw", "fnstsw", "fclex",
        "fnclex", "finit", "fninit", "fld1", "fldz", "fldpi", "fldl2e", "fldl2t",
        "fldlg2", "fldln2", "f2xm1", "fyl2x", "fyl2xp1", "fpatan", "fptan", "fsin",
        "fcos", "fsincos", "fprem", "fprem1", "frndint", "fxtract", "fnop",
        "eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp", "eip", "ax", "bx",
        "cx", "dx", "si", "di", "sp", "bp", "ip", "al", "ah", "bl", "bh", "cl", "ch",
        "dl", "dh", "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rsp", "rbp", "rip",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "r8d", "r9d", "r10d",
        "r11d", "r12d", "r13d", "r14d", "r15d", "r8w", "r9w", "r10w", "r11w", "r12w",
        "r13w", "r14w", "r15w", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b",
        "r15b", "sil", "dil", "bpl", "spl", "cs", "ds", "ss", "es", "fs", "gs",
        "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8",
        "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
        "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7", "ymm8",
        "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15",
        "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7", "zmm8",
        "zmm9", "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15",
        "cr0", "cr2", "cr3", "cr4", "cr8", "dr0", "dr1", "dr2", "dr3", "dr6", "dr7",
        "db", "dw", "dd", "dq", "dt", "do", "dy", "byte", "word", "dword", "qword",
        "tbyte", "oword", "yword", "zword", "resb", "resw", "resd", "resq", "rest",
        "reso", "resy", "equ", "org", "align", "section", "segment", "global",
        "extern", "public", "private", "proc", "endp", "local", "label", "macro",
        "endm", "endmacro", "rept", "endr", "times", "incbin", "include", "set",
        "near", "far", "short", "ptr", "type", "absolute", "bits", "use16", "use32",
        "use64", "default", "rel",
        "%define", "%undef", "%macro", "%endmacro", "%if", "%else", "%elif", "%endif",
        "%ifdef", "%ifndef", "%ifidn", "%ifidni", "%include", "%assign", "%strlen",
        "%substr", "%rep", "%endrep", "%rotate", "%error", "%warning",
        NULL};

static const char *m68k_keywords[] =
    {
        "move", "movea", "moveq", "movem", "movep", "movec", "moves", "lea", "pea",
        "clr", "ext", "extb", "neg", "negx", "not", "tst", "swap", "unlk", "link",
        "nop", "reset", "stop", "rte", "rts", "trap", "trapv", "illegal", "divs",
        "divu", "muls", "mulu", "exg", "cmp", "cmpa", "cmpi", "cmpm", "add", "adda",
        "addi", "addq", "addx", "sub", "suba", "subi", "subq", "subx", "and", "andi",
        "eor", "eori", "or", "ori", "asl", "asr", "lsl", "lsr", "rol", "ror", "roxl",
        "roxr", "btst", "bchg", "bclr", "bset", "bcc", "bcs", "beq", "bge", "bgt",
        "bhi", "ble", "bls", "blt", "bmi", "bne", "bpl", "bvc", "bvs", "bra", "bsr",
        "dbt", "dbf", "dbcc", "dbcs", "dbeq", "dbge", "dbgt", "dbhi", "dble", "dbls",
        "dblt", "dbmi", "dbne", "dbpl", "dbvc", "dbvs", "dbra", "jmp", "jsr",
        "chk", "chk2", "cmp2", "bkpt", "callm", "rtm", "pack", "unpk", "cas", "cas2",
        "divs.l", "divu.l", "divsl", "divul", "muls.l", "mulu.l", "mulsl", "mulul",
        "trapcc", "traphs", "trapls", "trapne", "trapeq", "trapvc", "trapvs", "trappl",
        "trapmi", "trapge", "traplt", "trapgt", "traple",
        "fmove", "fmovm", "fmovem", "fadd", "fsub", "fmul", "fdiv", "fabs", "fneg",
        "fsqrt", "fsin", "fcos", "ftan", "fasin", "facos", "fatan", "fatanh", "fsinh",
        "fcosh", "ftanh", "fetox", "fetoxm1", "flogn", "flognp1", "flog10", "flog2",
        "ftentox", "ftwotox", "fint", "fintrz", "fgetexp", "fgetman", "fscale", "frem",
        "fmod", "fsincos", "fcmp", "ftst", "fbeq", "fbne", "fbgt", "fblt", "fbge", "fble",
        "fbueq", "fbune", "fbugt", "fbult", "fbuge", "fbule", "fbt", "fbf", "fbseq",
        "fbsf", "fsave", "frestore",
        "pmove", "ptest", "pflush", "pflusha", "pload", "pvalid",
        "cinv", "cpush", "cinva", "cpusha",
        "move16",
        "sr", "ccr", "usp", "sp", "pc", "d0", "d1", "d2", "d3", "d4", "d5", "d6",
        "d7", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "fp",
        "fp0", "fp1", "fp2", "fp3", "fp4", "fp5", "fp6", "fp7",
        "data", "text", "bss", "section", "org", "equ", "dc", "dcb", "ds", "even",
        "end", "incbin", "include", "macro", "endm", "rept", "endr", "set",
        NULL};

static const char **keyword_tables[] =
    {
        c_keywords,    /* SYNTAX_LANG_C */
        cpp_keywords,  /* SYNTAX_LANG_CPP */
        x86_keywords,  /* SYNTAX_LANG_X86_ASM */
        m68k_keywords, /* SYNTAX_LANG_M68K_ASM */
        m68kc_keywords /* SYNTAX_LANG_M68K_C */
};

static int keyword_counts[SYNTAX_LANG_COUNT];
static int keywords_sorted = 0;

int ui_syntax_is_str_quote(wchar_t ch)
{
    return ch == L'"' || ch == (wchar_t)0x201C || ch == (wchar_t)0x201D;
}

int ui_syntax_is_char_quote(wchar_t ch)
{
    return ch == L'\'' || ch == (wchar_t)0x2018 || ch == (wchar_t)0x2019;
}

static int cmp_kw(const void *a, const void *b)
{
    const char *const *pa = (const char *const *)a;
    const char *const *pb = (const char *const *)b;

    return strcmp(*pa, *pb);
}

static void sort_keywords(void)
{
    int i;

    if (keywords_sorted)
        return;

    for (i = 0; i < SYNTAX_LANG_COUNT; i++)
    {
        int n = 0;

        while (keyword_tables[i][n])
            n++;

        keyword_counts[i] = n;

        qsort(keyword_tables[i], n, sizeof(const char *), cmp_kw);
    }

    keywords_sorted = 1;
}

static int is_keyword(const wchar_t *s, int len, int lang, int ci)
{
    int i;
    char ascii[64];

    if (len <= 0 || len >= (int)sizeof(ascii))
        return 0;

    for (i = 0; i < len; i++)
    {
        if (s[i] > 127)
            return 0;

        ascii[i] = (char)s[i];
    }

    ascii[len] = '\0';

    if (lang < 0 || lang >= SYNTAX_LANG_COUNT)
        return 0;

    sort_keywords();

    if (!ci)
    {
        const char *key = ascii;
        const char **res = (const char **)bsearch(&key, keyword_tables[lang], keyword_counts[lang], sizeof(const char *), cmp_kw);

        if (res)
            return 1;
    }
    else
    {
        for (i = 0; keyword_tables[lang][i] != NULL; i++)
        {
            if (strcasecmp(ascii, keyword_tables[lang][i]) == 0)
                return 1;
        }
    }

    return 0;
}

SyntaxLang ui_syntax_lang_from_filename(const char *filename)
{
    const char *p;

    if (!filename)
        return SYNTAX_LANG_NONE;

    p = strrchr(filename, '.');
    if (!p)
        return SYNTAX_LANG_NONE;

    if (strcasecmp(p, ".c") == 0)
        return SYNTAX_LANG_C;

    if (strcasecmp(p, ".h") == 0 || strcasecmp(p, ".cpp") == 0 || strcasecmp(p, ".cc") == 0 || strcasecmp(p, ".cxx") == 0 || strcasecmp(p, ".c++") == 0 || strcasecmp(p, ".hpp") == 0)
        return SYNTAX_LANG_CPP;

    if (strcasecmp(p, ".asm") == 0 || strcasecmp(p, ".s") == 0 || strcasecmp(p, ".nasm") == 0)
        return SYNTAX_LANG_X86_ASM;

    if (strcasecmp(p, ".x") == 0)
        return SYNTAX_LANG_M68K_ASM;

    return SYNTAX_LANG_NONE;
}

int ui_syntax_color_pair(SyntaxClass cls)
{
    switch (cls)
    {
    case SYNTAX_CLASS_KEYWORD:
        return COL_SYNTAX_KEYWORD;

    case SYNTAX_CLASS_STRING:
    case SYNTAX_CLASS_CHAR:
        return COL_SYNTAX_STRING;

    case SYNTAX_CLASS_COMMENT:
        return COL_SYNTAX_COMMENT;

    case SYNTAX_CLASS_NUMBER:
        return COL_SYNTAX_NUMBER;

    case SYNTAX_CLASS_PREPROC:
        return COL_SYNTAX_PREPROC;

    default:
        return COL_NORMAL;
    }
}

static int is_dec_digit(wchar_t ch)
{
    return ch >= L'0' && ch <= L'9';
}

static int is_hex_digit(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F');
}

static int is_number_char(wchar_t ch, int base)
{
    if (base == 16)
        return is_hex_digit(ch) || ch == L'x' || ch == L'X';

    if (base == 2)
        return ch == L'0' || ch == L'1' || ch == L'b' || ch == L'B';

    if (base == 8)
        return (ch >= L'0' && ch <= L'7') || ch == L'o' || ch == L'O';

    return iswdigit(ch) || ch == L'.' || ch == L'e' || ch == L'E' || ch == L'+' || ch == L'-';
}

static int classify_number(const wchar_t *line, int len, int i)
{
    int base = 10;
    int j;

    if (line[i] == L'0' && i + 1 < len)
    {
        if (line[i + 1] == L'x' || line[i + 1] == L'X')
            base = 16;
        else if (line[i + 1] == L'b' || line[i + 1] == L'B')
            base = 2;
        else if (line[i + 1] >= L'0' && line[i + 1] <= L'7')
            base = 8;
    }

    for (j = i; j < len; j++)
    {
        if (!is_number_char(line[j], base))
            break;
    }

    return j;
}

SyntaxState ui_syntax_classify(const wchar_t *line, int len, SyntaxClass *classes, SyntaxState start_state, SyntaxLang lang)
{
    int i = 0;
    SyntaxState state = start_state;

    while (i < len)
    {
        if (state == SYNTAX_STATE_COMMENT)
        {
            classes[i] = SYNTAX_CLASS_COMMENT;

            if (i + 1 < len && line[i] == L'*' && line[i + 1] == L'/')
            {
                classes[i + 1] = SYNTAX_CLASS_COMMENT;

                i += 2;
                state = SYNTAX_STATE_NORMAL;
            }
            else
            {
                i++;
            }
            continue;
        }

        if (state == SYNTAX_STATE_STRING)
        {
            classes[i] = SYNTAX_CLASS_STRING;

            if (line[i] == L'\\' && i + 1 < len)
            {
                classes[i + 1] = SYNTAX_CLASS_STRING;
                i += 2;
            }
            else if (ui_syntax_is_str_quote(line[i]))
            {
                i++;
                state = SYNTAX_STATE_NORMAL;
            }
            else
            {
                i++;
            }

            continue;
        }

        if (state == SYNTAX_STATE_CHAR)
        {
            classes[i] = SYNTAX_CLASS_CHAR;

            if (line[i] == L'\\' && i + 1 < len)
            {
                classes[i + 1] = SYNTAX_CLASS_CHAR;
                i += 2;
            }
            else if (ui_syntax_is_char_quote(line[i]))
            {
                i++;
                state = SYNTAX_STATE_NORMAL;
            }
            else
            {
                i++;
            }

            continue;
        }

        if (lang == SYNTAX_LANG_C || lang == SYNTAX_LANG_CPP || lang == SYNTAX_LANG_M68K_C)
        {
            if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'*')
            {
                classes[i] = SYNTAX_CLASS_COMMENT;
                classes[i + 1] = SYNTAX_CLASS_COMMENT;

                i += 2;
                state = SYNTAX_STATE_COMMENT;
                continue;
            }

            if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'/')
            {
                for (; i < len; i++)
                    classes[i] = SYNTAX_CLASS_COMMENT;

                continue;
            }

            if (ui_syntax_is_str_quote(line[i]))
            {
                classes[i] = SYNTAX_CLASS_STRING;

                i++;
                state = SYNTAX_STATE_STRING;
                continue;
            }

            if (ui_syntax_is_char_quote(line[i]))
            {
                classes[i] = SYNTAX_CLASS_CHAR;

                i++;
                state = SYNTAX_STATE_CHAR;
                continue;
            }

            if (line[i] == L'#')
            {
                for (; i < len; i++)
                    classes[i] = SYNTAX_CLASS_PREPROC;

                continue;
            }
        }
        else if (lang == SYNTAX_LANG_X86_ASM || lang == SYNTAX_LANG_M68K_ASM)
        {
            if (line[i] == L';')
            {
                for (; i < len; i++)
                    classes[i] = SYNTAX_CLASS_COMMENT;

                continue;
            }

            if (ui_syntax_is_str_quote(line[i]))
            {
                classes[i] = SYNTAX_CLASS_STRING;

                i++;
                state = SYNTAX_STATE_STRING;
                continue;
            }

            if (ui_syntax_is_char_quote(line[i]))
            {
                classes[i] = SYNTAX_CLASS_CHAR;

                i++;
                state = SYNTAX_STATE_CHAR;
                continue;
            }
        }

        if (iswdigit(line[i]) || (line[i] == L'$' && i + 1 < len && is_hex_digit(line[i + 1])))
        {
            int end = classify_number(line, len, i);

            for (; i < end; i++)
                classes[i] = SYNTAX_CLASS_NUMBER;

            continue;
        }

        if (iswalpha(line[i]) || line[i] == L'_')
        {
            int start = i;
            int end;

            for (end = start; end < len && (iswalnum(line[end]) || line[end] == L'_'); end++)
                ;

            if (lang >= 0 && lang < SYNTAX_LANG_COUNT && is_keyword(&line[start], end - start, lang, 0))
            {
                for (i = start; i < end; i++)
                    classes[i] = SYNTAX_CLASS_KEYWORD;
            }
            else if (lang == SYNTAX_LANG_M68K_C && is_keyword(&line[start], end - start, SYNTAX_LANG_C, 0))
            {
                for (i = start; i < end; i++)
                    classes[i] = SYNTAX_CLASS_KEYWORD;
            }
            else
            {
                for (i = start; i < end; i++)
                    classes[i] = SYNTAX_CLASS_NORMAL;
            }

            continue;
        }

        classes[i] = SYNTAX_CLASS_NORMAL;
        i++;
    }

    if (state == SYNTAX_STATE_STRING || state == SYNTAX_STATE_CHAR)
        state = SYNTAX_STATE_NORMAL;

    return state;
}
