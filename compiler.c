/*
 * A Language Compiler v2.4
 * Compiles A (.a files) to C, then to executable
 * 
 * Modes:
 *   optimized  - auto-closes blocks, 'end' optional (default)
 *   raw        - requires 'end' or '}' for all blocks
 *   debug      - optimized + machine-readable logging + auto-run
 *   debug_opt  - optimized + human-readable logging + auto-run
 *   debug_raw  - raw + human-readable logging + auto-run
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>

#define MAX_LINE 4096
#define MAX_VARS 1024
#define MAX_BLOCKS 256
#define MAX_FUNCS 512
#define MAX_ERRORS 256

/* ============== Types ============== */

typedef enum {
    MODE_OPTIMIZED,
    MODE_RAW,
    MODE_DEBUG,
    MODE_DEBUG_OPT,
    MODE_DEBUG_RAW
} CompileMode;

typedef enum {
    LOG_NONE,
    LOG_HUMAN,
    LOG_MACHINE
} LogMode;

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_LIST,
    TYPE_DICT,
    TYPE_TUPLE,
    TYPE_UNKNOWN
} VarType;

typedef struct {
    char name[256];
    VarType type;
    bool is_const;
} Variable;

typedef struct {
    int indent;
    int line_num;
    char type[32];
    bool closed_by_end;
    bool uses_braces;
} Block;

typedef struct {
    char name[256];
    char body[65536];
    int body_len;
} Function;

typedef struct {
    char message[512];
    int line_num;
    char severity[16];
} CompilerError;

/* ============== Globals ============== */

static Variable g_vars[MAX_VARS];
static int g_var_count = 0;

static Block g_blocks[MAX_BLOCKS];
static int g_block_depth = 0;

static Function g_funcs[MAX_FUNCS];
static int g_func_count = 0;

static CompilerError g_errors[MAX_ERRORS];
static int g_error_count = 0;

static int g_current_line = 0;
static CompileMode g_mode = MODE_OPTIMIZED;
static LogMode g_log_mode = LOG_NONE;
static bool g_in_function = false;
static int g_func_indent = 0;

static char g_main_code[262144];
static int g_main_len = 0;

static char g_output[524288];
static int g_output_len = 0;

/* ============== Logging System ============== */

static const char* type_to_string(VarType t) {
    switch (t) {
        case TYPE_INT: return "int";
        case TYPE_FLOAT: return "float";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        case TYPE_LIST: return "list";
        case TYPE_DICT: return "dict";
        case TYPE_TUPLE: return "tuple";
        default: return "unknown";
    }
}

static void log_var_decl(const char* name, VarType type, bool is_const, const char* value) {
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[36m[VARIABLE]\033[0m Line %d: Declaring %s%s variable '%s'",
                g_current_line, is_const ? "constant " : "", type_to_string(type), name);
        if (value && strlen(value) > 0) {
            fprintf(stderr, " with value: %s", value);
        }
        fprintf(stderr, "\n");
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "VAR_DECL:%d:%s:%s:%s:%s\n", 
                g_current_line, type_to_string(type), name, 
                is_const ? "const" : "mut",
                value ? value : "default");
    }
}

static void log_block_open(const char* type, const char* condition, bool uses_braces) {
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[32m[BLOCK OPEN]\033[0m Line %d: Opening '%s' block%s",
                g_current_line, type, uses_braces ? " with {}" : "");
        if (condition && strlen(condition) > 0) {
            fprintf(stderr, " with condition: %s", condition);
        }
        fprintf(stderr, " (depth: %d)\n", g_block_depth);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "BLOCK_OPEN:%d:%s:%d:%s:%s\n", 
                g_current_line, type, g_block_depth,
                uses_braces ? "braces" : "indent",
                condition ? condition : "none");
    }
}

static void log_block_close(const char* type, bool by_end, int orig_line, bool by_brace) {
    if (g_log_mode == LOG_HUMAN) {
        const char* method = by_brace ? "via '}'" : (by_end ? "via 'end' keyword" : "via auto-close");
        fprintf(stderr, "\033[33m[BLOCK CLOSE]\033[0m Line %d: Closing '%s' block (opened at line %d) %s (depth: %d)\n",
                g_current_line, type, orig_line, method, g_block_depth - 1);
    } else if (g_log_mode == LOG_MACHINE) {
        const char* method = by_brace ? "brace" : (by_end ? "explicit" : "auto");
        fprintf(stderr, "BLOCK_CLOSE:%d:%s:%d:%s:%d\n", 
                g_current_line, type, g_block_depth - 1, method, orig_line);
    }
}

static void log_func_decl(const char* name) {
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[35m[FUNCTION]\033[0m Line %d: Defining function '%s'\n",
                g_current_line, name);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "FUNC_DECL:%d:%s\n", g_current_line, name);
    }
}

static void log_func_call(const char* name) {
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[35m[CALL]\033[0m Line %d: Calling function '%s'\n",
                g_current_line, name);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "FUNC_CALL:%d:%s\n", g_current_line, name);
    }
}

static void log_print(const char* expr, VarType type) {
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[34m[PRINT]\033[0m Line %d: Printing %s expression: %s\n",
                g_current_line, type_to_string(type), expr);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "PRINT:%d:%s:%s\n", g_current_line, type_to_string(type), expr);
    }
}

static void log_statement(const char* stmt_type, const char* details) {
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[37m[STATEMENT]\033[0m Line %d: %s: %s\n",
                g_current_line, stmt_type, details);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "STMT:%d:%s:%s\n", g_current_line, stmt_type, details);
    }
}

static void log_parse_line(const char* line, int indent) {
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[90m[PARSE]\033[0m Line %d (indent=%d): %s\n",
                g_current_line, indent, line);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "PARSE:%d:%d:%s\n", g_current_line, indent, line);
    }
}

static void log_emit(const char* code) {
    if (g_log_mode == LOG_HUMAN) {
        char display[80];
        strncpy(display, code, 75);
        display[75] = '\0';
        for (int i = 0; display[i]; i++) {
            if (display[i] == '\n') display[i] = ' ';
        }
        if (strlen(code) > 75) strcat(display, "...");
        fprintf(stderr, "\033[90m[EMIT]\033[0m Line %d: -> %s\n", g_current_line, display);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "EMIT:%d:", g_current_line);
        for (const char* p = code; *p; p++) {
            if (*p == '\n') fprintf(stderr, "\\n");
            else if (*p == ':') fprintf(stderr, "\\:");
            else fputc(*p, stderr);
        }
        fprintf(stderr, "\n");
    }
}

static void log_for_in(const char* var, const char* iterable, VarType type) {
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[32m[FOR-IN]\033[0m Line %d: Iterating '%s' over %s '%s'\n",
                g_current_line, var, type_to_string(type), iterable);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "FOR_IN:%d:%s:%s:%s\n", g_current_line, var, iterable, type_to_string(type));
    }
}

static void log_run_start(void) {
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\n\033[1;32m========== RUNNING PROGRAM ==========\033[0m\n\n");
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "RUN_START\n");
    }
}

static void log_run_end(int exit_code) {
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\n\033[1;32m========== PROGRAM FINISHED ==========\033[0m\n");
        fprintf(stderr, "Exit code: %d\n", exit_code);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "RUN_END:%d\n", exit_code);
    }
}

/* ============== Error Handling ============== */

static void add_error(const char* msg, const char* severity) {
    if (g_error_count < MAX_ERRORS) {
        strncpy(g_errors[g_error_count].message, msg, 511);
        g_errors[g_error_count].message[511] = '\0';
        g_errors[g_error_count].line_num = g_current_line;
        strncpy(g_errors[g_error_count].severity, severity, 15);
        g_error_count++;
    }
    
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[31m[%s]\033[0m Line %d: %s\n",
                strcmp(severity, "error") == 0 ? "ERROR" : "WARNING",
                g_current_line, msg);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "%s:%d:%s\n",
                strcmp(severity, "error") == 0 ? "ERR" : "WARN",
                g_current_line, msg);
    }
}

static void error(const char* msg) {
    add_error(msg, "error");
}

static void warning(const char* msg) {
    add_error(msg, "warning");
}

static void print_all_errors(void) {
    if (g_error_count == 0) return;
    
    fprintf(stderr, "\n========== Compilation Results ==========\n");
    fprintf(stderr, "Found %d issue(s):\n\n", g_error_count);
    
    int error_num = 0;
    int warning_num = 0;
    
    for (int i = 0; i < g_error_count; i++) {
        if (strcmp(g_errors[i].severity, "error") == 0) {
            error_num++;
            fprintf(stderr, "[ERROR %d] Line %d: %s\n", 
                    error_num, g_errors[i].line_num, g_errors[i].message);
        } else {
            warning_num++;
            fprintf(stderr, "[WARNING %d] Line %d: %s\n", 
                    warning_num, g_errors[i].line_num, g_errors[i].message);
        }
    }
    
    fprintf(stderr, "\n=========================================\n");
    fprintf(stderr, "Summary: %d error(s), %d warning(s)\n", error_num, warning_num);
}

static bool has_errors(void) {
    for (int i = 0; i < g_error_count; i++) {
        if (strcmp(g_errors[i].severity, "error") == 0) {
            return true;
        }
    }
    return false;
}

/* ============== Utility Functions ============== */

static bool is_raw_mode(void) {
    return g_mode == MODE_RAW || g_mode == MODE_DEBUG_RAW;
}

static bool is_debug_mode(void) {
    return g_mode == MODE_DEBUG || g_mode == MODE_DEBUG_OPT || g_mode == MODE_DEBUG_RAW;
}

static char* trim_left(char* str) {
    while (*str && isspace((unsigned char)*str)) str++;
    return str;
}

static char* trim(char* str) {
    char* start = str;
    while (*start && isspace((unsigned char)*start)) start++;
    if (*start == 0) return start;
    
    char* end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return start;
}

static int get_indent(const char* line) {
    int count = 0;
    while (*line) {
        if (*line == ' ') count++;
        else if (*line == '\t') count += 4;
        else break;
        line++;
    }
    return count;
}

static bool starts_with(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static bool is_empty_or_comment(const char* line) {
    const char* p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    return (*p == '\0' || *p == '#');
}

static bool ends_with_brace(const char* line) {
    const char* p = line + strlen(line) - 1;
    while (p >= line && isspace((unsigned char)*p)) p--;
    return (p >= line && *p == '{');
}

static bool is_closing_brace(const char* line) {
    const char* p = trim_left((char*)line);
    return (*p == '}' && (*(p+1) == '\0' || isspace((unsigned char)*(p+1)) || *(p+1) == '#'));
}

static char* strip_trailing_brace(char* line) {
    char* p = line + strlen(line) - 1;
    while (p >= line && isspace((unsigned char)*p)) p--;
    if (p >= line && *p == '{') {
        *p = '\0';
        // Also remove trailing whitespace before the brace
        p--;
        while (p >= line && isspace((unsigned char)*p)) {
            *p = '\0';
            p--;
        }
    }
    return line;
}

static void append_output(const char* str) {
    int len = strlen(str);
    if (g_output_len + len < (int)sizeof(g_output) - 1) {
        strcpy(g_output + g_output_len, str);
        g_output_len += len;
    }
}

static void append_main(const char* str) {
    int len = strlen(str);
    if (g_main_len + len < (int)sizeof(g_main_code) - 1) {
        strcpy(g_main_code + g_main_len, str);
        g_main_len += len;
    }
}

static void append_func(const char* str) {
    if (g_func_count > 0) {
        Function* f = &g_funcs[g_func_count - 1];
        int len = strlen(str);
        if (f->body_len + len < (int)sizeof(f->body) - 1) {
            strcpy(f->body + f->body_len, str);
            f->body_len += len;
        }
    }
}

static void emit(const char* str) {
    if (g_in_function) {
        append_func(str);
    } else {
        append_main(str);
    }
    log_emit(str);
}

static void emit_no_log(const char* str) {
    if (g_in_function) {
        append_func(str);
    } else {
        append_main(str);
    }
}

/* ============== Variable Management ============== */

static VarType get_var_type(const char* name) {
    for (int i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            return g_vars[i].type;
        }
    }
    return TYPE_UNKNOWN;
}

static void register_var(const char* name, VarType type, bool is_const) {
    for (int i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            g_vars[i].type = type;
            g_vars[i].is_const = is_const;
            return;
        }
    }
    
    if (g_var_count < MAX_VARS) {
        strncpy(g_vars[g_var_count].name, name, 255);
        g_vars[g_var_count].type = type;
        g_vars[g_var_count].is_const = is_const;
        g_var_count++;
    } else {
        error("Maximum variable limit reached");
    }
}

static VarType infer_expr_type(const char* expr) {
    char* e = trim((char*)expr);
    
    if (e[0] == '"') return TYPE_STRING;
    if (strcmp(e, "true") == 0 || strcmp(e, "false") == 0) return TYPE_BOOL;
    if (e[0] == '(' && strchr(e, ',')) return TYPE_TUPLE;
    if (e[0] == '[') return TYPE_LIST;
    if (e[0] == '{') return TYPE_DICT;
    
    if (strchr(e, '.') && !strchr(e, '"')) {
        bool is_num = true;
        for (int i = 0; e[i]; i++) {
            if (!isdigit(e[i]) && e[i] != '.' && e[i] != '-') {
                is_num = false;
                break;
            }
        }
        if (is_num) return TYPE_FLOAT;
    }
    
    bool is_int = true;
    for (int i = 0; e[i]; i++) {
        if (!isdigit(e[i]) && e[i] != '-') {
            is_int = false;
            break;
        }
    }
    if (is_int && strlen(e) > 0) return TYPE_INT;
    
    char var_name[256];
    int j = 0;
    for (int i = 0; e[i] && (isalnum(e[i]) || e[i] == '_'); i++) {
        var_name[j++] = e[i];
    }
    var_name[j] = '\0';
    
    VarType vt = get_var_type(var_name);
    if (vt != TYPE_UNKNOWN) return vt;
    
    if (strchr(e, '[')) {
        char base[256];
        sscanf(e, "%[^[]", base);
        VarType bt = get_var_type(trim(base));
        if (bt == TYPE_LIST) return TYPE_INT;
        if (bt == TYPE_STRING) return TYPE_INT; // char as int
    }
    
    return TYPE_INT;
}

/* ============== Block Management ============== */

static void push_block(int indent, const char* type, const char* condition, bool uses_braces) {
    if (g_block_depth < MAX_BLOCKS) {
        g_blocks[g_block_depth].indent = indent;
        g_blocks[g_block_depth].line_num = g_current_line;
        strncpy(g_blocks[g_block_depth].type, type, 31);
        g_blocks[g_block_depth].closed_by_end = false;
        g_blocks[g_block_depth].uses_braces = uses_braces;
        g_block_depth++;
        log_block_open(type, condition, uses_braces);
    } else {
        error("Maximum block nesting depth exceeded");
    }
}

static void close_block(bool by_end, bool by_brace) {
    if (g_block_depth > 0) {
        log_block_close(g_blocks[g_block_depth - 1].type, by_end, 
                        g_blocks[g_block_depth - 1].line_num, by_brace);
        if (strcmp(g_blocks[g_block_depth - 1].type, "func") == 0) {
            g_in_function = false;
        }
        g_block_depth--;
        emit_no_log("}\n");
    }
}

static void close_brace_block(void) {
    if (g_block_depth > 0) {
        if (!g_blocks[g_block_depth - 1].uses_braces) {
            warning("Closing '}' for block not opened with '{'");
        }
        close_block(false, true);
    } else {
        error("'}' without matching '{'");
    }
}

static void auto_close_blocks_to_indent(int new_indent) {
    while (g_block_depth > 0 && 
           g_blocks[g_block_depth - 1].indent >= new_indent &&
           !g_blocks[g_block_depth - 1].closed_by_end &&
           !g_blocks[g_block_depth - 1].uses_braces) {
        
        if (strcmp(g_blocks[g_block_depth - 1].type, "func") == 0) {
            if (new_indent <= g_blocks[g_block_depth - 1].indent) {
                close_block(false, false);
            }
            break;
        }
        
        close_block(false, false);
    }
}

/* ============== Time Function Replacement ============== */

static void replace_time_funcs(char* line) {
    char buffer[MAX_LINE];
    char* p = line;
    char* out = buffer;
    
    while (*p) {
        if (strncmp(p, "time.now()", 10) == 0) {
            strcpy(out, "(int)time(NULL)");
            out += 15;
            p += 10;
        } else if (strncmp(p, "date.now()", 10) == 0) {
            strcpy(out, "(int)time(NULL)");
            out += 15;
            p += 10;
        } else if (strncmp(p, "clock.now()", 11) == 0) {
            strcpy(out, "((double)clock() / CLOCKS_PER_SEC)");
            out += 34;
            p += 11;
        } else {
            *out++ = *p++;
        }
    }
    *out = '\0';
    strcpy(line, buffer);
}

/* ============== Statement Handlers ============== */

static void handle_variable_decl(char* line, bool is_const) {
    char type_str[32], name[256], value[MAX_LINE] = {0};
    char* p = line;
    
    if (is_const) {
        p = trim_left(p + 5);
    }
    
    VarType vt = TYPE_UNKNOWN;
    
    if (starts_with(p, "int ")) {
        strcpy(type_str, "int");
        vt = TYPE_INT;
        p += 4;
    } else if (starts_with(p, "float ")) {
        strcpy(type_str, "float");
        vt = TYPE_FLOAT;
        p += 6;
    } else if (starts_with(p, "bool ")) {
        strcpy(type_str, "bool");
        vt = TYPE_BOOL;
        p += 5;
    } else if (starts_with(p, "string ")) {
        strcpy(type_str, "char*");
        vt = TYPE_STRING;
        p += 7;
    } else if (starts_with(p, "list ")) {
        strcpy(type_str, "List");
        vt = TYPE_LIST;
        p += 5;
    } else if (starts_with(p, "dict ")) {
        strcpy(type_str, "Dict");
        vt = TYPE_DICT;
        p += 5;
    } else if (starts_with(p, "tuple ")) {
        strcpy(type_str, "Tuple");
        vt = TYPE_TUPLE;
        p += 6;
    } else {
        error("Unknown type in variable declaration");
        return;
    }
    
    p = trim_left(p);
    
    int i = 0;
    while (*p && (isalnum(*p) || *p == '_')) {
        name[i++] = *p++;
    }
    name[i] = '\0';
    
    if (strlen(name) == 0) {
        error("Missing variable name in declaration");
        return;
    }
    
    p = trim_left(p);
    
    register_var(name, vt, is_const);
    
    char emit_buf[MAX_LINE];
    
    if (*p == '=') {
        p++;
        p = trim_left(p);
        if (strlen(p) == 0) {
            error("Missing value after '=' in variable declaration");
            strcpy(value, "0");
        } else {
            strncpy(value, p, MAX_LINE - 1);
            replace_time_funcs(value);
        }
        
        snprintf(emit_buf, sizeof(emit_buf), "%s%s %s = %s;\n",
                 is_const ? "const " : "", type_str, name, value);
    } else {
        const char* def_val = "";
        if (vt == TYPE_INT) def_val = "0";
        else if (vt == TYPE_STRING) def_val = "NULL";
        else if (vt == TYPE_LIST) def_val = "new_list()";
        else if (vt == TYPE_DICT) def_val = "new_dict()";
        else if (vt == TYPE_TUPLE) def_val = "new_tuple()";
        
        strcpy(value, def_val);
        
        if (strlen(def_val) > 0) {
            snprintf(emit_buf, sizeof(emit_buf), "%s%s %s = %s;\n",
                     is_const ? "const " : "", type_str, name, def_val);
        } else {
            snprintf(emit_buf, sizeof(emit_buf), "%s%s %s;\n",
                     is_const ? "const " : "", type_str, name);
        }
    }
    
    log_var_decl(name, vt, is_const, value);
    emit_no_log(emit_buf);
}

static void handle_print(char* line) {
    char* start = strchr(line, '(');
    if (!start) {
        error("Missing '(' in print statement");
        return;
    }
    start++;
    
    char* end = strrchr(line, ')');
    if (!end) {
        error("Missing ')' in print statement");
        return;
    }
    
    char expr[MAX_LINE];
    int len = end - start;
    strncpy(expr, start, len);
    expr[len] = '\0';
    trim(expr);
    
    if (strlen(expr) == 0) {
        error("Empty print statement");
        return;
    }
    
    replace_time_funcs(expr);
    
    VarType type = infer_expr_type(expr);
    log_print(expr, type);
    
    char emit_buf[MAX_LINE];
    
    switch (type) {
        case TYPE_STRING:
            snprintf(emit_buf, sizeof(emit_buf), "printf(\"%%s\\n\", %s);\n", expr);
            break;
        case TYPE_BOOL:
            snprintf(emit_buf, sizeof(emit_buf), 
                     "printf(\"%%s\\n\", (%s) ? \"true\" : \"false\");\n", expr);
            break;
        case TYPE_FLOAT:
            snprintf(emit_buf, sizeof(emit_buf), "printf(\"%%f\\n\", %s);\n", expr);
            break;
        case TYPE_LIST:
            snprintf(emit_buf, sizeof(emit_buf), "print_list(&%s);\n", expr);
            break;
        case TYPE_TUPLE:
            snprintf(emit_buf, sizeof(emit_buf), "print_tuple(&%s);\n", expr);
            break;
        default:
            snprintf(emit_buf, sizeof(emit_buf), "printf(\"%%d\\n\", (int)(%s));\n", expr);
            break;
    }
    
    emit_no_log(emit_buf);
}

static void handle_if(char* line, bool has_brace) {
    char* p = trim_left(line);
    p += 2;
    p = trim_left(p);
    
    if (has_brace) {
        strip_trailing_brace(p);
    }
    
    char* colon = strrchr(p, ':');
    if (colon) {
        *colon = '\0';
    }
    
    p = trim(p);
    
    if (strlen(p) == 0) {
        error("Missing condition in if statement");
        p = "1";
    }
    
    char condition[MAX_LINE];
    strncpy(condition, p, MAX_LINE - 1);
    replace_time_funcs(p);
    
    char emit_buf[MAX_LINE];
    snprintf(emit_buf, sizeof(emit_buf), "if (%s) {\n", p);
    emit_no_log(emit_buf);
    
    push_block(get_indent(line), "if", condition, has_brace);
}

static void handle_elif(char* line, bool has_brace) {
    if (g_block_depth == 0 || 
        (strcmp(g_blocks[g_block_depth - 1].type, "if") != 0 &&
         strcmp(g_blocks[g_block_depth - 1].type, "elif") != 0)) {
        error("'elif' without matching 'if'");
    }
    
    char* p = trim_left(line);
    p += 4;
    p = trim_left(p);
    
    if (has_brace) {
        strip_trailing_brace(p);
    }
    
    char* colon = strrchr(p, ':');
    if (colon) {
        *colon = '\0';
    }
    
    p = trim(p);
    
    if (strlen(p) == 0) {
        error("Missing condition in elif statement");
        p = "1";
    }
    
    char condition[MAX_LINE];
    strncpy(condition, p, MAX_LINE - 1);
    replace_time_funcs(p);
    
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[33m[BLOCK CHAIN]\033[0m Line %d: Continuing if-chain with 'elif' condition: %s\n",
                g_current_line, condition);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "BLOCK_CHAIN:%d:elif:%s\n", g_current_line, condition);
    }
    
    char emit_buf[MAX_LINE];
    snprintf(emit_buf, sizeof(emit_buf), "} else if (%s) {\n", p);
    emit_no_log(emit_buf);
    
    if (g_block_depth > 0) {
        strcpy(g_blocks[g_block_depth - 1].type, "elif");
        g_blocks[g_block_depth - 1].uses_braces = has_brace;
    }
}

static void handle_else(char* line, bool has_brace) {
    (void)line;
    
    if (g_block_depth == 0 || 
        (strcmp(g_blocks[g_block_depth - 1].type, "if") != 0 &&
         strcmp(g_blocks[g_block_depth - 1].type, "elif") != 0)) {
        error("'else' without matching 'if' or 'elif'");
    }
    
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[33m[BLOCK CHAIN]\033[0m Line %d: Continuing if-chain with 'else'\n",
                g_current_line);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "BLOCK_CHAIN:%d:else\n", g_current_line);
    }
    
    emit_no_log("} else {\n");
    
    if (g_block_depth > 0) {
        strcpy(g_blocks[g_block_depth - 1].type, "else");
        g_blocks[g_block_depth - 1].uses_braces = has_brace;
    }
}

static void handle_while(char* line, bool has_brace) {
    char* p = trim_left(line);
    p += 5;
    p = trim_left(p);
    
    if (has_brace) {
        strip_trailing_brace(p);
    }
    
    char* colon = strrchr(p, ':');
    if (colon) {
        *colon = '\0';
    }
    
    p = trim(p);
    
    if (strlen(p) == 0) {
        error("Missing condition in while statement");
        p = "0";
    }
    
    char condition[MAX_LINE];
    strncpy(condition, p, MAX_LINE - 1);
    replace_time_funcs(p);
    
    char emit_buf[MAX_LINE];
    snprintf(emit_buf, sizeof(emit_buf), "while (%s) {\n", p);
    emit_no_log(emit_buf);
    
    push_block(get_indent(line), "while", condition, has_brace);
}

/* Handle for-in iteration: for var in iterable: */
static void handle_for_in(char* line, bool has_brace) {
    char* p = trim_left(line);
    p += 3;  // skip "for"
    p = trim_left(p);
    
    // Extract variable name
    char var[64] = {0};
    int i = 0;
    while (*p && (isalnum(*p) || *p == '_')) {
        if (i < 63) var[i++] = *p;
        p++;
    }
    var[i] = '\0';
    
    if (strlen(var) == 0) {
        error("Missing loop variable in for-in statement");
        strcpy(var, "_item");
    }
    
    // Skip " in "
    p = trim_left(p);
    if (strncmp(p, "in", 2) == 0) {
        p += 2;
        p = trim_left(p);
    } else {
        error("Missing 'in' keyword in for-in statement");
    }
    
    // Get iterable (until : or { or end of string)
    char iterable[256] = {0};
    i = 0;
    while (*p && *p != ':' && *p != '{' && !isspace(*p)) {
        if (i < 255) iterable[i++] = *p;
        p++;
    }
    iterable[i] = '\0';
    trim(iterable);
    
    if (strlen(iterable) == 0) {
        error("Missing iterable in for-in statement");
        strcpy(iterable, "\"\"");
    }
    
    // Determine iterable type
    VarType iter_type = infer_expr_type(iterable);
    
    // For string literals, override type detection
    if (iterable[0] == '"') {
        iter_type = TYPE_STRING;
    }
    
    log_for_in(var, iterable, iter_type);
    
    char emit_buf[MAX_LINE * 2];
    char idx_var[80];
    snprintf(idx_var, sizeof(idx_var), "_%s_idx", var);
    
    switch (iter_type) {
        case TYPE_STRING:
            // Iterate over characters in string
            snprintf(emit_buf, sizeof(emit_buf),
                "{ char* _%s_str = %s;\n"
                "for (int %s = 0; _%s_str[%s]; %s++) {\n"
                "    char %s = _%s_str[%s];\n",
                var, iterable,
                idx_var, var, idx_var, idx_var,
                var, var, idx_var);
            register_var(var, TYPE_INT, false);  // char as int
            break;
            
        case TYPE_LIST:
            // Iterate over list elements
            snprintf(emit_buf, sizeof(emit_buf),
                "for (int %s = 0; %s < %s.size; %s++) {\n"
                "    int %s = %s.data[%s];\n",
                idx_var, idx_var, iterable, idx_var,
                var, iterable, idx_var);
            register_var(var, TYPE_INT, false);
            break;
            
        case TYPE_DICT:
            // Iterate over dict keys
            snprintf(emit_buf, sizeof(emit_buf),
                "for (int %s = 0; %s < %s.size; %s++) {\n"
                "    char* %s = %s.keys[%s];\n",
                idx_var, idx_var, iterable, idx_var,
                var, iterable, idx_var);
            register_var(var, TYPE_STRING, false);
            break;
            
        case TYPE_TUPLE:
            // Iterate over tuple elements
            snprintf(emit_buf, sizeof(emit_buf),
                "for (int %s = 0; %s < %s.size; %s++) {\n"
                "    int %s = %s.data[%s];\n",
                idx_var, idx_var, iterable, idx_var,
                var, iterable, idx_var);
            register_var(var, TYPE_INT, false);
            break;
            
        default:
            // Assume string-like for unknown types
            snprintf(emit_buf, sizeof(emit_buf),
                "{ char* _%s_str = (char*)(%s);\n"
                "for (int %s = 0; _%s_str && _%s_str[%s]; %s++) {\n"
                "    char %s = _%s_str[%s];\n",
                var, iterable,
                idx_var, var, var, idx_var, idx_var,
                var, var, idx_var);
            register_var(var, TYPE_INT, false);
            break;
    }
    
    emit_no_log(emit_buf);
    
    char condition[MAX_LINE];
    snprintf(condition, sizeof(condition), "%s in %s", var, iterable);
    push_block(get_indent(line), "for_in", condition, has_brace);
}

static void handle_for(char* line, bool has_brace) {
    char* p = trim_left(line);
    p += 3;
    p = trim_left(p);
    
    // Check if this is a for-in loop
    char* in_keyword = strstr(p, " in ");
    if (in_keyword) {
        handle_for_in(line, has_brace);
        return;
    }
    
    if (has_brace) {
        strip_trailing_brace(p);
    }
    
    char* colon = strrchr(p, ':');
    if (colon) {
        *colon = '\0';
    }
    
    p = trim(p);
    
    char var[64] = {0}, start_val[64] = {0}, end_val[64] = {0}, step[64] = "1";
    
    int i = 0;
    while (*p && (isalnum(*p) || *p == '_')) {
        if (i < 63) var[i++] = *p;
        p++;
    }
    var[i] = '\0';
    
    if (strlen(var) == 0) {
        error("Missing loop variable in for statement");
        strcpy(var, "_i");
    }
    
    p = trim_left(p);
    if (*p == '=') {
        p++;
    } else {
        error("Missing '=' in for loop");
    }
    p = trim_left(p);
    
    i = 0;
    while (*p && !isspace(*p) && strncmp(p, "to", 2) != 0) {
        if (i < 63) start_val[i++] = *p;
        p++;
    }
    start_val[i] = '\0';
    
    if (strlen(start_val) == 0) {
        error("Missing start value in for loop");
        strcpy(start_val, "0");
    }
    
    p = trim_left(p);
    
    if (!starts_with(p, "to")) {
        error("Missing 'to' keyword in for loop");
    } else {
        p += 2;
    }
    
    if (*p == '(') {
        p++;
        i = 0;
        while (*p && *p != ')') {
            if (i < 63) step[i++] = *p;
            p++;
        }
        step[i] = '\0';
        if (*p == ')') {
            p++;
        } else {
            error("Missing ')' in for loop step");
        }
        
        if (strlen(step) == 0) {
            error("Empty step value in for loop");
            strcpy(step, "1");
        }
    }
    
    p = trim_left(p);
    strncpy(end_val, trim(p), 63);
    end_val[63] = '\0';
    
    if (strlen(end_val) == 0) {
        error("Missing end value in for loop");
        strcpy(end_val, "0");
    }
    
    replace_time_funcs(start_val);
    replace_time_funcs(end_val);
    replace_time_funcs(step);
    
    char condition[MAX_LINE];
    snprintf(condition, sizeof(condition), "%s = %s to %s step %s", var, start_val, end_val, step);
    
    char emit_buf[MAX_LINE];
    if (strcmp(step, "1") == 0) {
        snprintf(emit_buf, sizeof(emit_buf), 
                 "for (int %s = %s; %s <= %s; %s++) {\n",
                 var, start_val, var, end_val, var);
    } else {
        snprintf(emit_buf, sizeof(emit_buf), 
                 "for (int %s = %s; %s <= %s; %s += %s) {\n",
                 var, start_val, var, end_val, var, step);
    }
    emit_no_log(emit_buf);
    
    register_var(var, TYPE_INT, false);
    push_block(get_indent(line), "for", condition, has_brace);
}

static void handle_func(char* line, bool has_brace) {
    char* p = trim_left(line);
    p += 4;
    p = trim_left(p);
    
    if (has_brace) {
        strip_trailing_brace(p);
    }
    
    char* colon = strchr(p, ':');
    if (colon) {
        *colon = '\0';
    }
    
    char name[256];
    int i = 0;
    while (*p && (isalnum(*p) || *p == '_')) {
        if (i < 255) name[i++] = *p;
        p++;
    }
    name[i] = '\0';
    
    if (strlen(name) == 0) {
        error("Missing function name");
        return;
    }
    
    if (strcmp(name, "main") == 0) {
        warning("'func main' is ignored - compiler generates its own main()");
        g_in_function = false;
        return;
    }
    
    for (int j = 0; j < g_func_count; j++) {
        if (strcmp(g_funcs[j].name, name) == 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Duplicate function definition: '%s'", name);
            error(msg);
        }
    }
    
    log_func_decl(name);
    
    if (g_func_count < MAX_FUNCS) {
        strcpy(g_funcs[g_func_count].name, name);
        g_funcs[g_func_count].body[0] = '\0';
        g_funcs[g_func_count].body_len = 0;
        g_func_count++;
    } else {
        error("Maximum function limit reached");
    }
    
    g_in_function = true;
    g_func_indent = get_indent(line);
    
    push_block(g_func_indent, "func", name, has_brace);
}

static void handle_append(char* line) {
    char* p = strchr(line, '(');
    if (!p) {
        error("Missing '(' in append statement");
        return;
    }
    p++;
    
    char* end = strrchr(line, ')');
    if (!end) {
        error("Missing ')' in append statement");
        return;
    }
    
    char args[MAX_LINE];
    int len = end - p;
    strncpy(args, p, len);
    args[len] = '\0';
    
    char* comma = strchr(args, ',');
    if (!comma) {
        error("Missing ',' in append - expected: append(list, value)");
        return;
    }
    
    *comma = '\0';
    char* list_name = trim(args);
    char* value = trim(comma + 1);
    
    if (strlen(list_name) == 0) {
        error("Missing list name in append");
        return;
    }
    
    if (strlen(value) == 0) {
        error("Missing value in append");
        return;
    }
    
    VarType lt = get_var_type(list_name);
    if (lt != TYPE_LIST && lt != TYPE_UNKNOWN) {
        char msg[512];
        snprintf(msg, sizeof(msg), "'%s' is not a list", list_name);
        error(msg);
    }
    
    replace_time_funcs(value);
    
    log_statement("append", list_name);
    
    char emit_buf[MAX_LINE];
    snprintf(emit_buf, sizeof(emit_buf), "list_append(&%s, %s);\n", list_name, value);
    emit_no_log(emit_buf);
}

static void handle_end(void) {
    if (g_block_depth > 0) {
        if (g_blocks[g_block_depth - 1].uses_braces) {
            warning("Using 'end' to close block opened with '{' - use '}' instead");
        }
        close_block(true, false);
        
        // Close extra scope for for_in string iteration
        if (g_block_depth >= 0 && strcmp(g_blocks[g_block_depth].type, "for_in") == 0) {
            emit_no_log("}\n");
        }
    } else {
        error("'end' without matching block");
    }
}

static void handle_raw_statement(char* line) {
    char* p = trim(line);
    if (!*p) return;
    
    replace_time_funcs(p);
    
    char first_word[256];
    int i = 0;
    const char* pp = p;
    while (*pp && (isalnum(*pp) || *pp == '_')) {
        if (i < 255) first_word[i++] = *pp;
        pp++;
    }
    first_word[i] = '\0';
    
    bool is_func_call = false;
    for (int j = 0; j < g_func_count; j++) {
        if (strcmp(g_funcs[j].name, first_word) == 0) {
            is_func_call = true;
            break;
        }
    }
    
    if (is_func_call) {
        log_func_call(first_word);
    } else {
        log_statement("raw", p);
    }
    
    char buffer[MAX_LINE];
    char* out = buffer;
    char* in = p;
    
    while (*in) {
        if (isalpha(*in) || *in == '_') {
            char var[256];
            i = 0;
            while (*in && (isalnum(*in) || *in == '_')) {
                if (i < 255) var[i++] = *in;
                in++;
            }
            var[i] = '\0';
            
            if (*in == '[' && get_var_type(var) == TYPE_LIST) {
                strcpy(out, var);
                out += strlen(var);
                strcpy(out, ".data");
                out += 5;
            } else {
                strcpy(out, var);
                out += strlen(var);
            }
        } else {
            *out++ = *in++;
        }
    }
    *out = '\0';
    
    strcat(buffer, ";\n");
    emit_no_log(buffer);
}

/* ============== Main Processing ============== */

static void process_line(char* original_line) {
    char line[MAX_LINE];
    strncpy(line, original_line, MAX_LINE - 1);
    line[MAX_LINE - 1] = '\0';
    
    g_current_line++;
    
    char* comment = strchr(line, '#');
    if (comment) *comment = '\0';
    
    if (is_empty_or_comment(line)) return;
    
    int indent = get_indent(line);
    char* trimmed = trim_left(line);
    
    log_parse_line(trim(trimmed), indent);
    
    // Handle closing brace
    if (is_closing_brace(trimmed)) {
        close_brace_block();
        return;
    }
    
    if (strcmp(trim(trimmed), "end") == 0) {
        handle_end();
        return;
    }
    
    // Check if line ends with opening brace
    bool has_brace = ends_with_brace(trimmed);
    
    if (!is_raw_mode() && g_block_depth > 0) {
        if (!starts_with(trimmed, "elif") && !starts_with(trimmed, "else")) {
            auto_close_blocks_to_indent(indent);
        }
    }
    
    char* t = trimmed;
    
    if (starts_with(t, "const ")) {
        handle_variable_decl(t, true);
    }
    else if (starts_with(t, "int ") || starts_with(t, "float ") || 
             starts_with(t, "bool ") || starts_with(t, "string ") ||
             starts_with(t, "list ") || starts_with(t, "dict ") ||
             starts_with(t, "tuple ")) {
        handle_variable_decl(t, false);
    }
    else if (starts_with(t, "print(")) {
        handle_print(t);
    }
    else if (starts_with(t, "if ")) {
        handle_if(original_line, has_brace);
    }
    else if (starts_with(t, "elif ")) {
        handle_elif(t, has_brace);
    }
    else if (starts_with(t, "else:") || starts_with(t, "else {") || 
             starts_with(t, "else:") || strcmp(trim(t), "else") == 0) {
        handle_else(t, has_brace);
    }
    else if (starts_with(t, "while ")) {
        handle_while(original_line, has_brace);
    }
    else if (starts_with(t, "for ")) {
        handle_for(original_line, has_brace);
    }
    else if (starts_with(t, "func ")) {
        handle_func(original_line, has_brace);
    }
    else if (starts_with(t, "append(")) {
        handle_append(t);
    }
    else if (starts_with(t, "dset(") || starts_with(t, "dget(")) {
        log_statement("dict_op", t);
        emit_no_log(t);
        emit_no_log(";\n");
    }
    else {
        handle_raw_statement(t);
    }
}

/* ============== Standard Library ============== */

static const char* STDLIB = 
"#include <stdio.h>\n"
"#include <stdlib.h>\n"
"#include <string.h>\n"
"#include <stdbool.h>\n"
"#include <stdarg.h>\n"
"#include <math.h>\n"
"#include <time.h>\n"
"#include <setjmp.h>\n"
"\n"
"/* List implementation */\n"
"typedef struct {\n"
"    int* data;\n"
"    int size;\n"
"    int cap;\n"
"} List;\n"
"\n"
"static List new_list(void) {\n"
"    List l;\n"
"    l.cap = 8;\n"
"    l.size = 0;\n"
"    l.data = (int*)malloc(sizeof(int) * l.cap);\n"
"    return l;\n"
"}\n"
"\n"
"static void list_append(List* l, int val) {\n"
"    if (l->size >= l->cap) {\n"
"        l->cap *= 2;\n"
"        l->data = (int*)realloc(l->data, sizeof(int) * l->cap);\n"
"    }\n"
"    l->data[l->size++] = val;\n"
"}\n"
"\n"
"static void list_free(List* l) {\n"
"    free(l->data);\n"
"    l->data = NULL;\n"
"    l->size = 0;\n"
"    l->cap = 0;\n"
"}\n"
"\n"
"static int list_len(List* l) {\n"
"    return l->size;\n"
"}\n"
"\n"
"static void print_list(List* l) {\n"
"    printf(\"[\");\n"
"    for (int i = 0; i < l->size; i++) {\n"
"        printf(\"%d\", l->data[i]);\n"
"        if (i < l->size - 1) printf(\", \");\n"
"    }\n"
"    printf(\"]\\n\");\n"
"}\n"
"\n"
"static int* slice_arr(int* arr, int start, int end, int* out_len) {\n"
"    *out_len = end - start;\n"
"    int* result = (int*)malloc(sizeof(int) * (*out_len));\n"
"    for (int i = 0; i < *out_len; i++) {\n"
"        result[i] = arr[start + i];\n"
"    }\n"
"    return result;\n"
"}\n"
"\n"
"/* Tuple implementation */\n"
"typedef struct {\n"
"    int* data;\n"
"    int size;\n"
"} Tuple;\n"
"\n"
"static Tuple new_tuple(void) {\n"
"    Tuple t;\n"
"    t.size = 0;\n"
"    t.data = NULL;\n"
"    return t;\n"
"}\n"
"\n"
"static Tuple make_tuple(int count, ...) {\n"
"    Tuple t;\n"
"    t.size = count;\n"
"    t.data = (int*)malloc(sizeof(int) * count);\n"
"    va_list args;\n"
"    va_start(args, count);\n"
"    for (int i = 0; i < count; i++) {\n"
"        t.data[i] = va_arg(args, int);\n"
"    }\n"
"    va_end(args);\n"
"    return t;\n"
"}\n"
"\n"
"static void print_tuple(Tuple* t) {\n"
"    printf(\"(\");\n"
"    for (int i = 0; i < t->size; i++) {\n"
"        printf(\"%d\", t->data[i]);\n"
"        if (i < t->size - 1) printf(\", \");\n"
"    }\n"
"    printf(\")\\n\");\n"
"}\n"
"\n"
"static void tuple_free(Tuple* t) {\n"
"    free(t->data);\n"
"    t->data = NULL;\n"
"    t->size = 0;\n"
"}\n"
"\n"
"/* Dictionary implementation */\n"
"#define DICT_MAX 256\n"
"\n"
"typedef struct {\n"
"    char* keys[DICT_MAX];\n"
"    int vals[DICT_MAX];\n"
"    int size;\n"
"} Dict;\n"
"\n"
"static Dict new_dict(void) {\n"
"    Dict d;\n"
"    d.size = 0;\n"
"    for (int i = 0; i < DICT_MAX; i++) {\n"
"        d.keys[i] = NULL;\n"
"    }\n"
"    return d;\n"
"}\n"
"\n"
"static void dset(Dict* d, const char* key, int val) {\n"
"    for (int i = 0; i < d->size; i++) {\n"
"        if (d->keys[i] && strcmp(d->keys[i], key) == 0) {\n"
"            d->vals[i] = val;\n"
"            return;\n"
"        }\n"
"    }\n"
"    if (d->size < DICT_MAX) {\n"
"        d->keys[d->size] = strdup(key);\n"
"        d->vals[d->size] = val;\n"
"        d->size++;\n"
"    }\n"
"}\n"
"\n"
"static int dget(Dict* d, const char* key) {\n"
"    for (int i = 0; i < d->size; i++) {\n"
"        if (d->keys[i] && strcmp(d->keys[i], key) == 0) {\n"
"            return d->vals[i];\n"
"        }\n"
"    }\n"
"    return 0;\n"
"}\n"
"\n"
"static void dict_free(Dict* d) {\n"
"    for (int i = 0; i < d->size; i++) {\n"
"        free(d->keys[i]);\n"
"    }\n"
"    d->size = 0;\n"
"}\n"
"\n";

/* ============== File Compilation ============== */

static void compile_file(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        exit(1);
    }
    
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\n\033[1m========== COMPILATION LOG ==========\033[0m\n\n");
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "LOG_START:%s\n", filename);
    }
    
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (len > 1 && line[len-2] == '\r') line[len-2] = '\0';
        
        process_line(line);
    }
    
    fclose(fp);
    
    int saved_line = g_current_line;
    while (g_block_depth > 0) {
        g_current_line = g_blocks[g_block_depth - 1].line_num;
        
        if (is_raw_mode() || g_blocks[g_block_depth - 1].uses_braces) {
            char msg[512];
            snprintf(msg, sizeof(msg), 
                     "Unclosed '%s' block started at line %d - missing '%s'",
                     g_blocks[g_block_depth - 1].type,
                     g_blocks[g_block_depth - 1].line_num,
                     g_blocks[g_block_depth - 1].uses_braces ? "}" : "end");
            error(msg);
        }
        
        close_block(false, false);
    }
    g_current_line = saved_line;
    
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\n\033[1m========== END OF LOG ==========\033[0m\n\n");
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "LOG_END:%d\n", g_current_line);
    }
}

static void generate_output(void) {
    append_output(STDLIB);
    
    for (int i = 0; i < g_func_count; i++) {
        append_output("void ");
        append_output(g_funcs[i].name);
        append_output("(void);\n");
    }
    append_output("\n");
    
    for (int i = 0; i < g_func_count; i++) {
        append_output("void ");
        append_output(g_funcs[i].name);
        append_output("(void) {\n");
        append_output(g_funcs[i].body);
        append_output("}\n\n");
    }
    
    append_output("int main(void) {\n");
    append_output(g_main_code);
    append_output("    return 0;\n");
    append_output("}\n");
}

static void write_c_file(const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", filename);
        exit(1);
    }
    
    fputs(g_output, fp);
    fclose(fp);
}

static void compile_c_to_binary(const char* c_file, CompileMode mode) {
    char cmd[1024];
    const char* flags;
    
    switch (mode) {
        case MODE_DEBUG:
        case MODE_DEBUG_OPT:
            flags = "-Ofast -g";
            break;
        case MODE_RAW:
        case MODE_DEBUG_RAW:
            flags = "-O1 -g";
            break;
        case MODE_OPTIMIZED:
        default:
            flags = "-Ofast -w";
            break;
    }
    
    snprintf(cmd, sizeof(cmd), "gcc %s %s -o program -lm 2>&1", flags, c_file);
    
    if (g_log_mode == LOG_HUMAN) {
        fprintf(stderr, "\033[36m[GCC]\033[0m Running: %s\n", cmd);
    } else if (g_log_mode == LOG_MACHINE) {
        fprintf(stderr, "GCC_CMD:%s\n", cmd);
    }
    
    int result = system(cmd);
    if (result != 0) {
        error("GCC compilation failed - check generated C code");
    }
}

static void run_program(void) {
    log_run_start();
    
    fflush(stdout);
    fflush(stderr);
    
    int result = system("./program");
    int exit_code = WEXITSTATUS(result);
    
    log_run_end(exit_code);
}

static const char* mode_to_string(CompileMode mode) {
    switch (mode) {
        case MODE_OPTIMIZED: return "optimized";
        case MODE_RAW: return "raw";
        case MODE_DEBUG: return "debug";
        case MODE_DEBUG_OPT: return "debug_opt";
        case MODE_DEBUG_RAW: return "debug_raw";
        default: return "unknown";
    }
}

/* ============== Main ============== */

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("A Language Compiler v2.4\n");
        printf("Usage: %s <file.a> [mode]\n\n", argv[0]);
        printf("Modes:\n");
        printf("  optimized (default) - Auto-closes blocks, 'end' optional\n");
        printf("  raw                 - Requires 'end' or '}' for all blocks\n");
        printf("  debug               - Optimized + machine-readable logging + auto-run\n");
        printf("  debug_opt           - Optimized + human-readable logging + auto-run\n");
        printf("  debug_raw           - Raw + human-readable logging + auto-run\n");
        printf("\nNew features:\n");
        printf("  - Curly braces: 'if x > 0 {' ... '}'\n");
        printf("  - For-in loops: 'for c in string:', 'for x in list:', 'for k in dict:'\n");
        return 1;
    }
    
    const char* input_file = argv[1];
    
    g_mode = MODE_OPTIMIZED;
    g_log_mode = LOG_NONE;
    
    if (argc >= 3) {
        if (strcmp(argv[2], "debug") == 0) {
            g_mode = MODE_DEBUG;
            g_log_mode = LOG_MACHINE;
        } else if (strcmp(argv[2], "debug_opt") == 0) {
            g_mode = MODE_DEBUG_OPT;
            g_log_mode = LOG_HUMAN;
        } else if (strcmp(argv[2], "debug_raw") == 0) {
            g_mode = MODE_DEBUG_RAW;
            g_log_mode = LOG_HUMAN;
        } else if (strcmp(argv[2], "raw") == 0) {
            g_mode = MODE_RAW;
        } else if (strcmp(argv[2], "optimized") == 0) {
            g_mode = MODE_OPTIMIZED;
        } else {
            fprintf(stderr, "Unknown mode: %s\n", argv[2]);
            return 1;
        }
    }
    
    // Initialize
    g_var_count = 0;
    g_block_depth = 0;
    g_func_count = 0;
    g_error_count = 0;
    g_current_line = 0;
    g_in_function = false;
    g_main_len = 0;
    g_output_len = 0;
    g_main_code[0] = '\0';
    g_output[0] = '\0';
    
    // Compile
    printf("Compiling %s (mode: %s)...\n", input_file, mode_to_string(g_mode));
    
    compile_file(input_file);
    generate_output();
    
    // Check for errors
    if (has_errors()) {
        print_all_errors();
        fprintf(stderr, "\nCompilation failed.\n");
        return 1;
    }
    
    // Write C file
    const char* c_file = "output.c";
    write_c_file(c_file);
    printf("Generated %s\n", c_file);
    
    // Compile to binary
    compile_c_to_binary(c_file, g_mode);
    
    // Check again for errors from GCC stage
    if (has_errors()) {
        print_all_errors();
        fprintf(stderr, "\nCompilation failed.\n");
        return 1;
    }
    
    printf("Generated executable: program\n");
    
    // Print warnings if any
    if (g_error_count > 0) {
        print_all_errors();
    }
    
    // Auto-run in all debug modes
    if (is_debug_mode()) {
        run_program();
    }
    
    return 0;
}
