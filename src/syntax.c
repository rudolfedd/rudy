/* syntax.c — language definitions and highlighting cache. */
#include "rudy.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------------------------------------------------------- Table */

#define C_KEYWORDS \
    "auto break case char const continue default do double else enum extern " \
    "float for goto if inline int long register restrict return short signed " \
    "sizeof static struct switch typedef union unsigned void volatile while " \
    "_Bool _Complex _Atomic"

#define C_TYPES \
    "int char short long float double void signed unsigned bool size_t ssize_t " \
    "ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t " \
    "FILE va_list va_arg"

#define C_CONSTS "NULL true false"

#define C_BUILTINS \
    "printf fprintf sprintf snprintf vprintf vfprintf scanf sscanf fscanf " \
    "malloc calloc realloc free memcpy memmove memset memcmp strlen strnlen " \
    "strcpy strncpy strcat strncat strcmp strncmp strchr strrchr strstr strtok " \
    "abort exit atoi atol atof strtol strtod rand srand qsort bsearch assert " \
    "fopen fclose fread fwrite fgets fputs fgetc fputc fseek ftell rewind " \
    "remove rename system getenv time clock localtime gmtime strftime"

static Language LANGS[] = {
    {
        .name = "C",
        .exts = "c,h",
        .keywords = C_KEYWORDS,
        .types = C_TYPES,
        .consts = C_CONSTS,
        .builtins = C_BUILTINS,
        .labels = "",
        .regs = "",
        .line_comment = "//",
        .block_open = "/*",
        .block_close = "*/",
        .flags = LF_PREPROC
    },
    {
        .name = "C++",
        .exts = "cpp,cxx,cc,c++,hpp,hxx,hh,h++",
        .keywords =
            "alignas alignof asm auto bool break case catch char class const " \
            "constexpr const_cast continue decltype default delete do double " \
            "dynamic_cast else enum explicit export extern false float for friend " \
            "goto if inline int long mutable namespace new noexcept nullptr " \
            "operator private protected public register reinterpret_cast return " \
            "short signed sizeof static static_assert static_cast struct switch " \
            "template this thread_local throw true try typedef typeid typename " \
            "union unsigned using virtual void volatile while",
        .types =
            "std string vector map unordered_map set multiset deque list queue " \
            "stack array pair tuple optional variant size_t int8_t int16_t " \
            "int32_t int64_t uint8_t uint16_t uint32_t uint64_t unique_ptr " \
            "shared_ptr weak_ptr function ostream istream",
        .consts = "NULL true false",
        .builtins =
            "cout cin cerr clog endl sort find copy move begin end push_back " \
            "pop_back size length empty clear substr atoi atof stoi stod to_string",
        .labels = "",
        .regs = "",
        .line_comment = "//",
        .block_open = "/*",
        .block_close = "*/",
        .flags = LF_PREPROC
    },
    {
        .name = "C#",
        .exts = "cs,csx",
        .keywords =
            "abstract as async await base break case catch checked class const " \
            "continue default delegate do else enum event explicit extern " \
            "finally fixed for foreach get global goto if implicit in init " \
            "interface internal is lock namespace new null operator out override " \
            "params partial private protected public readonly record ref " \
            "required return sealed set sizeof stackalloc static struct switch " \
            "this throw try typeof unchecked unsafe using var virtual void " \
            "volatile while yield",
        .types =
            "int float double decimal bool byte char string object sbyte short " \
            "uint ulong ushort dynamic nint nuint void",
        .consts = "true false null",
        .builtins =
            "Console WriteLine Write ReadLine ReadKey Math Max Min Abs Sqrt Pow " \
            "File ReadAllText ReadAllLines WriteAllText WriteAllLines " \
            "Directory CreateDirectory FileStream StreamReader StreamWriter " \
            "List Dictionary HashSet Queue Stack Task Thread Exception Convert " \
            "DateTime DateTimeOffset Guid Random StringBuilder",
        .labels = "",
        .regs = "",
        .line_comment = "//",
        .block_open = "/*",
        .block_close = "*/",
        .flags = LF_VERBATIM | LF_CAPTYPE
    },
    {
        .name = "Rust",
        .exts = "rs",
        .keywords =
            "as async await break const continue crate dyn else enum extern fn " \
            "for if impl in let loop match mod move mut pub ref return self " \
            "Self static struct super trait type unsafe use where while box",
        .types =
            "i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool " \
            "char str String Vec Option Result Box Rc Arc RefCell Cell Mutex " \
            "RwLock HashMap HashSet BTreeMap BTreeSet VecDeque HashMap HashSet " \
            "Cow PhantomData",
        .consts = "true false None Some Ok Err",
        .builtins =
            "println print format vec panic assert assert_eq assert_ne write " \
            "writeln dbg todo unimplemented matches unwrap expect clone into " \
            "iter collect map filter fold enumerate zip min max abs pow sqrt " \
            "is_empty len push insert remove get contains sort reverse",
        .labels = "",
        .regs = "",
        .line_comment = "//",
        .block_open = "/*",
        .block_close = "*/",
        .flags = LF_RAWSTR | LF_LIFETIME
    },
    {
        .name = "NASM",
        .exts = "asm,nasm",
        .keywords =
            "mov movzx movsx movsxd lea add sub adc sbb mul imul div idiv inc dec " \
            "neg not and or xor shl shr sal sar rol ror rcl rcr push pop call ret " \
            "retn retf leave nop int iret hlt jmp je jne jz jnz jg jge jl jle ja " \
            "jae jb jbe js jns jc jnc jo jno jp jnp repe repne rep repz loop loope " \
            "loopne jcxz jecxz jrcxz xchg cmpxchg bsf bsr bt bts btr btc cdq cwde " \
            "cbw cwd shld shrd stos movs lods scas cmps test cmp db dw dq resb " \
            "resw resd times equ section global default bits use16 use32 use64 " \
            "cpu float dword word byte ptr offset",
        .types = "",
        .consts = "",
        .builtins = "",
        .labels = ".text .data .bss .rodata .note .comment .init .fini",
        .regs =
            "rax rbx rcx rdx rsi rdi rbp rsp r8 r9 r10 r11 r12 r13 r14 r15 " \
            "eax ebx ecx edx esi edi ebp esp eip ax bx cx dx si di bp sp al ah " \
            "bl bh cl ch dl dh sil dil bpl spl r8b r8w r8d r9b r9w r9d r10b r10w " \
            "r10d r11b r11w r11d r12b r12w r12d r13b r13w r13d r14b r14w r14d " \
            "r15b r15w r15d xmm0 xmm1 xmm2 xmm3 xmm4 xmm5 xmm6 xmm7 xmm8 xmm9 " \
            "xmm10 xmm11 xmm12 xmm13 xmm14 xmm15 ymm0 ymm1 ymm2 ymm3 zmm0 zmm1 " \
            "cr0 cr2 cr3 cr4 cr8 dr0 dr1 dr2 dr3 dr6 dr7 st0 st1 st2 st3 st4 st5 " \
            "st6 st7 mm0 mm1 mm2 mm3",
        .line_comment = ";",
        .block_open = NULL,
        .block_close = NULL,
        .flags = LF_ASM
    },
    {
        .name = "Java",
        .exts = "java",
        .keywords =
            "abstract assert boolean break byte case catch char class const " \
            "continue default do double else enum extends final finally float for " \
            "if implements import instanceof int interface long native new package " \
            "private protected public return short static strictfp super switch " \
            "synchronized this throw throws transient try void volatile while true " \
            "false null var record sealed permits yield",
        .types =
            "String Integer Double Long Short Byte Character Float Boolean Object " \
            "Math System Exception RuntimeException List ArrayList LinkedList Map " \
            "HashMap TreeMap Set HashSet TreeSet Optional Stream Thread Runnable " \
            "StringBuilder StringBuffer",
        .consts = "",
        .builtins =
            "println print printf equals toString hashCode getClass length " \
            "add remove contains size clear isEmpty Arrays Collections sort " \
            "forEach stream of",
        .labels = "",
        .regs = "",
        .line_comment = "//",
        .block_open = "/*",
        .block_close = "*/",
        .flags = LF_TEXTBLK | LF_CAPTYPE
    },
    {
        .name = "Text",
        .exts = "txt,log,md,conf,cfg,ini,toml,json,yml,yaml",
        .keywords = "",
        .types = "",
        .consts = "",
        .builtins = "",
        .labels = "",
        .regs = "",
        .line_comment = NULL,
        .block_open = NULL,
        .block_close = NULL,
        .flags = 0
    }
};

#define LANG_COUNT ((int)(sizeof LANGS / sizeof LANGS[0]))

/* --------------------------------------------------------- Initialization */

void syntax_init(void)
{
    for (int i = 0; i < LANG_COUNT; i++) {
        Language *L = &LANGS[i];
        ws_init(&L->kw, L->keywords);
        ws_init(&L->ty, L->types);
        ws_init(&L->cn, L->consts);
        ws_init(&L->bi, L->builtins);
        ws_init(&L->lb, L->labels);
        ws_init(&L->rg, L->regs);
    }
}

int lang_count(void) { return LANG_COUNT; }

const Language *lang_at(int i)
{
    if (i < 0 || i >= LANG_COUNT) return &LANGS[LANG_COUNT - 1];
    return &LANGS[i];
}

static int lower_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb) return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

const Language *lang_by_name(const char *name)
{
    if (!name || !*name) return NULL;
    for (int i = 0; i < LANG_COUNT; i++)
        if (lower_cmp(LANGS[i].name, name) == 0) return &LANGS[i];
    /* synonyms */
    if (lower_cmp(name, "c++") == 0 || lower_cmp(name, "cpp") == 0) return &LANGS[1];
    if (lower_cmp(name, "csharp") == 0 || lower_cmp(name, "c#") == 0) return &LANGS[2];
    if (lower_cmp(name, "rust") == 0) return &LANGS[3];
    if (lower_cmp(name, "asm") == 0 || lower_cmp(name, "nasm") == 0) return &LANGS[4];
    if (lower_cmp(name, "java") == 0) return &LANGS[5];
    if (lower_cmp(name, "text") == 0 || lower_cmp(name, "plain") == 0) return &LANGS[6];
    return NULL;
}

const Language *lang_by_path(const char *path)
{
    if (!path) return &LANGS[LANG_COUNT - 1];
    const char *dot = strrchr(path, '.');
    if (!dot) return &LANGS[LANG_COUNT - 1];
    dot++;
    for (int i = 0; i < LANG_COUNT; i++) {
        const char *p = LANGS[i].exts;
        while (*p) {
            const char *e = strchr(p, ',');
            size_t      n = e ? (size_t)(e - p) : strlen(p);
            if (strlen(dot) == n) {
                int match = 1;
                for (size_t k = 0; k < n; k++)
                    if (tolower((unsigned char)dot[k]) != tolower((unsigned char)p[k])) { match = 0; break; }
                if (match) return &LANGS[i];
            }
            if (!e) break;
            p = e + 1;
        }
    }
    return &LANGS[LANG_COUNT - 1];
}
