#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <stdbool.h>

#define MAX_LINE 4096
#define MAX_VARS 512
#define MAX_DEPTH 128
#define INITIAL_CAP 4096

// --- TYPES ---
typedef enum { VAR_UNKNOWN, VAR_INT, VAR_STRING, VAR_LIST, VAR_BOOL, VAR_FLOAT } VarType;

typedef struct {
    char name[64];
    VarType type;
} Symbol;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StringBuffer;

// --- GLOBALS ---
int SCOPE_DEPTH = 0; 
int IN_USER_FUNC = 0; 
int indent_stack[MAX_DEPTH];
int scope_lines[MAX_DEPTH]; // New: Tracks line numbers of open blocks
int func_start_line = 0;    // New: Tracks line number of current function

Symbol symbol_table[MAX_VARS];
int symbol_count = 0;

// --- STD LIB ---
const char* STD_LIB = 
"#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n#include <stdbool.h>\n#include <setjmp.h>\n#include <time.h>\n"
"jmp_buf _env[64];int _esp=0;char _err[256];\n" 
"typedef struct{int keys[512];int vals[512];int size;}Dict;\n"
"void dset(Dict*d,int k,int v){for(int i=0;i<d->size;i++){if(d->keys[i]==k){d->vals[i]=v;return;}}if(d->size<512){d->keys[d->size]=k;d->vals[d->size]=v;d->size++;}}\n"
"int dget(Dict*d,int k){for(int i=0;i<d->size;i++){if(d->keys[i]==k)return d->vals[i];}return 0;}\n"
"typedef struct{int*data;int size;int cap;}List;\n"
"List new_list(){List l;l.size=0;l.cap=4;l.data=(int*)malloc(16);return l;}\n"
"void append(List*l,int v){if(l->size>=l->cap){l->cap*=2;l->data=(int*)realloc(l->data,l->cap*sizeof(int));}l->data[l->size++]=v;}\n"
"int* slice_arr(int*a,int s,int e){int l=e-s;if(l<0)l=0;int*r=malloc((l+1)*sizeof(int));memcpy(r,a+s,l*sizeof(int));return r;}\n";

// --- STRING BUFFER ---
void sb_init(StringBuffer* sb) {
    sb->cap = INITIAL_CAP;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    sb->data[0] = '\0';
}

void sb_append(StringBuffer* sb, const char* str) {
    size_t l = strlen(str);
    if (sb->len + l + 1 >= sb->cap) {
        sb->cap = (sb->cap * 2) + l;
        sb->data = (char*)realloc(sb->data, sb->cap);
    }
    memcpy(sb->data + sb->len, str, l);
    sb->len += l;
    sb->data[sb->len] = '\0';
}

void sb_printf(StringBuffer* sb, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int size = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (size < 0) return;

    if (sb->len + size + 1 >= sb->cap) {
        sb->cap = (sb->cap * 2) + size;
        sb->data = (char*)realloc(sb->data, sb->cap);
    }

    va_start(args, fmt);
    vsnprintf(sb->data + sb->len, size + 1, fmt, args);
    va_end(args);
    sb->len += size;
}

void sb_free(StringBuffer* sb) {
    free(sb->data);
}

// --- SYMBOL TABLE ---
void register_var(const char* name, VarType type) {
    if (symbol_count >= MAX_VARS) return;
    for(int i=0; i<symbol_count; i++) {
        if(strcmp(symbol_table[i].name, name) == 0) return;
    }
    strcpy(symbol_table[symbol_count].name, name);
    symbol_table[symbol_count].type = type;
    symbol_count++;
}

VarType get_var_type(const char* name) {
    for (int i = 0; i < symbol_count; i++) {
        if (strcmp(symbol_table[i].name, name) == 0) return symbol_table[i].type;
    }
    return VAR_UNKNOWN;
}

// --- HELPERS ---
char* trim(char* str) {
    if (!str) return NULL;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    char* end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    *(end+1) = 0;
    return str;
}

int get_indentation(const char* line) {
    int count = 0;
    while (*line) {
        if (*line == ' ') count++;
        else if (*line == '\t') count += 4;
        else break;
        line++;
    }
    return count;
}

void replace_phrase(char* src, const char* find, const char* replace) {
    char buffer[MAX_LINE];
    char* p;
    size_t find_len = strlen(find);
    size_t replace_len = strlen(replace);
    
    if (!(p = strstr(src, find))) return;

    do {
        size_t prefix_len = p - src;
        if (prefix_len + replace_len + strlen(p + find_len) >= MAX_LINE) break;

        memcpy(buffer, src, prefix_len);
        memcpy(buffer + prefix_len, replace, replace_len);
        strcpy(buffer + prefix_len + replace_len, p + find_len);
        strcpy(src, buffer);
        
        p = strstr(src + prefix_len + replace_len, find);
    } while (p);
}

void format_list_access(char* dest, const char* src) {
    dest[0] = '\0';
    const char* start = src;
    char* brk = strchr(start, '[');

    if (!brk) { strcpy(dest, src); return; }

    while (brk) {
        strncat(dest, start, brk - start);
        const char* p = brk - 1;
        while (p >= src && isspace((unsigned char)*p)) p--;
        const char* end_word = p;
        while (p >= src && (isalnum((unsigned char)*p) || *p == '_')) p--;
        const char* start_word = p + 1;

        char var_name[64];
        int len = (int)(end_word - start_word + 1);
        if (len > 0 && len < 64) {
            strncpy(var_name, start_word, len);
            var_name[len] = '\0';
            if (get_var_type(var_name) == VAR_LIST) strcat(dest, ".data[");
            else strcat(dest, "[");
        } else {
            strcat(dest, "[");
        }
        start = brk + 1;
        brk = strchr(start, '[');
    }
    strcat(dest, start);
}

// --- COMPILER ---
int compile_to_c(const char* filename, int auto_close, int verbose) {
    FILE* f = fopen(filename, "rb");
    if (!f) { printf("Error: Could not open file %s\n", filename); return 1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* source_code = malloc(fsize + 1);
    fread(source_code, 1, fsize, f);
    fclose(f);
    source_code[fsize] = 0;

    StringBuffer func_buff; sb_init(&func_buff);
    StringBuffer main_buff; sb_init(&main_buff);
    StringBuffer* current = &main_buff;

    symbol_count = 0;
    SCOPE_DEPTH = 0;
    IN_USER_FUNC = 0;
    func_start_line = 0;
    memset(indent_stack, 0, sizeof(indent_stack));
    memset(scope_lines, 0, sizeof(scope_lines));

    char* line = strtok(source_code, "\n");
    char scratch[MAX_LINE]; 
    int line_num = 0;

    while (line != NULL) {
        line_num++;
        
        // 1. Pre-process line (strip comments)
        char* comment = strchr(line, '#');
        if (comment) *comment = '\0';

        // 2. Get info
        int current_indent = get_indentation(line);
        char* trimmed = trim(line); 

        // Skip empty lines
        if (strlen(trimmed) == 0) {
            line = strtok(NULL, "\n");
            continue;
        }

        // 3. Safety Copy
        strncpy(scratch, trimmed, MAX_LINE - 1); scratch[MAX_LINE-1]='\0';

        // 4. Auto-Close Logic (Indent Based)
        int is_manual_end = (strcmp(scratch, "end") == 0);
        int is_continuation = (strncmp(scratch, "else", 4) == 0 || strncmp(scratch, "elif", 4) == 0);

        if (auto_close && !is_manual_end) {
            while (SCOPE_DEPTH > 0) {
                int opener_indent = indent_stack[SCOPE_DEPTH - 1];
                
                if (current_indent < opener_indent) {
                    sb_append(current, "    }\n");
                    SCOPE_DEPTH--;
                } 
                else if (current_indent == opener_indent) {
                    if (is_continuation) break; 
                    else {
                        sb_append(current, "    }\n");
                        SCOPE_DEPTH--;
                    }
                } 
                else {
                    break;
                }
            }

            if (IN_USER_FUNC && SCOPE_DEPTH == 0 && current_indent == 0) {
                sb_append(current, "}\n\n");
                current = &main_buff;
                IN_USER_FUNC = 0;
            }
        }

        // --- PARSING ---

        // Entry point skip
        if (strncmp(scratch, "func main", 9) == 0) { line = strtok(NULL, "\n"); continue; }

        // Time replacements
        if (strstr(scratch, ".now()")) {
            replace_phrase(scratch, "time.now()", "(int)time(NULL)");
            replace_phrase(scratch, "date.now()", "(int)time(NULL)");
            replace_phrase(scratch, "clock.now()", "((double)clock()/CLOCKS_PER_SEC)");
        }

        // Functions
        if (strncmp(scratch, "func ", 5) == 0) {
            char* c = strchr(scratch, ':'); if (c) *c=0;
            sb_printf(&func_buff, "void %s() {\n", trim(scratch + 5));
            current = &func_buff;
            IN_USER_FUNC = 1;
            func_start_line = line_num; // Track start line
            line = strtok(NULL, "\n"); continue;
        }

        // Manual End
        if (strcmp(scratch, "end") == 0) {
            if (SCOPE_DEPTH > 0) {
                sb_append(current, "    }\n");
                SCOPE_DEPTH--;
            } else if (IN_USER_FUNC) {
                sb_append(current, "}\n\n");
                current = &main_buff;
                IN_USER_FUNC = 0;
            }
            line = strtok(NULL, "\n"); continue;
        }

        // Blocks
        if (strncmp(scratch, "for", 3) == 0) {
            char v[32], s[32], e[32];
            char* eq = strchr(scratch, '=');
            char* to = strstr(scratch, " to ");
            char* col = strchr(scratch, ':'); if(col)*col=0;
            if(eq && to) {
                *eq=0; *to=0;
                strcpy(v, trim(scratch+3)); strcpy(s, trim(eq+1)); strcpy(e, trim(to+4));
                sb_printf(current, "    for(int %s=%s; %s<=%s; %s++){\n", v,s,v,e,v);
                
                // Track Scope Line
                indent_stack[SCOPE_DEPTH] = current_indent;
                scope_lines[SCOPE_DEPTH] = line_num; 
                SCOPE_DEPTH++;
            }
            line = strtok(NULL, "\n"); continue;
        }
        if (strncmp(scratch, "if", 2) == 0 || strncmp(scratch, "while", 5) == 0) {
            char* key = (scratch[0]=='i') ? "if" : "while";
            int off = (scratch[0]=='i') ? 2 : 5;
            char* c=strchr(scratch,':'); if(c)*c=0;
            sb_printf(current, "    %s (%s) {\n", key, trim(scratch+off));
            
            // Track Scope Line
            indent_stack[SCOPE_DEPTH] = current_indent;
            scope_lines[SCOPE_DEPTH] = line_num;
            SCOPE_DEPTH++;
            
            line = strtok(NULL, "\n"); continue;
        }
        
        // Else / Elif
        if (strncmp(scratch, "elif", 4) == 0) { 
            char* c=strchr(scratch,':'); if(c)*c=0; 
            sb_printf(current, "    } else if (%s) {\n", trim(scratch+4)); 
            line = strtok(NULL, "\n"); continue;
        }
        if (strncmp(scratch, "else", 4) == 0) { 
            sb_append(current, "    } else {\n"); 
            line = strtok(NULL, "\n"); continue; 
        }

        // Variables
        if (strncmp(scratch, "int ", 4) == 0 || strncmp(scratch, "bool ", 5) == 0 || 
            strncmp(scratch, "string ", 7) == 0 || strncmp(scratch, "list ", 5) == 0 ||
            strncmp(scratch, "float ", 6) == 0 || strncmp(scratch, "const ", 6) == 0) {
            
            char type_str[10], name[64], val[2048];
            char* ptr = scratch;
            int is_const = 0;
            if (strncmp(ptr, "const", 5) == 0) { is_const = 1; ptr += 6; }
            
            char* eq = strchr(ptr, '=');
            if (eq) {
                *eq = 0;
                char* space = strchr(ptr, ' ');
                if (space) {
                    *space = 0;
                    strcpy(type_str, ptr);
                    strcpy(name, trim(space+1));
                } else { sscanf(ptr, "%s", type_str); strcpy(name, trim(ptr + strlen(type_str))); }
                strcpy(val, trim(eq+1));

                if (strcmp(type_str, "int") == 0) sb_printf(current, "    %sint %s = %s;\n", is_const?"const ":"", name, val);
                else if (strcmp(type_str, "bool") == 0) { register_var(name, VAR_BOOL); sb_printf(current, "    %sbool %s = %s;\n", is_const?"const ":"", name, val); }
                else if (strcmp(type_str, "float") == 0) { register_var(name, VAR_FLOAT); sb_printf(current, "    %sfloat %s = %s;\n", is_const?"const ":"", name, val); }
                else if (strcmp(type_str, "string") == 0) { register_var(name, VAR_STRING); sb_printf(current, "    %schar* %s = %s;\n", is_const?"const ":"", name, val); }
                else if (strcmp(type_str, "list") == 0) { register_var(name, VAR_LIST); sb_printf(current, "    List %s = new_list();\n", name); }
            } else {
                sscanf(ptr, "%s", type_str); 
                strcpy(name, trim(ptr + strlen(type_str)));
                if (strcmp(type_str, "int") == 0) sb_printf(current, "    int %s = 0;\n", name);
                else if (strcmp(type_str, "string") == 0) sb_printf(current, "    char* %s = NULL;\n", name);
                else sb_printf(current, "    %s %s;\n", type_str, name); 
            }
            line = strtok(NULL, "\n"); continue;
        }

        // Print
        if (strncmp(scratch, "print", 5) == 0 && strncmp(scratch, "printf", 6) != 0) {
            char* s = strchr(scratch, '(');
            char* e = strrchr(scratch, ')');
            if (s && e) {
                *e = 0; char* content = trim(s+1);
                VarType t = get_var_type(content);
                if (strchr(content, '"') || t == VAR_STRING) sb_printf(current, "    printf(\"%%s\\n\", %s);\n", content);
                else if (t == VAR_BOOL || strcmp(content,"true")==0 || strcmp(content,"false")==0) sb_printf(current, "    printf(\"%%s\\n\", (%s)?\"true\":\"false\");\n", content);
                else if (t == VAR_FLOAT) sb_printf(current, "    printf(\"%%f\\n\", %s);\n", content);
                else { char fmt[MAX_LINE]; format_list_access(fmt, content); sb_printf(current, "    printf(\"%%d\\n\", (int)(%s));\n", fmt); }
            }
            line = strtok(NULL, "\n"); continue;
        }

        char fmt[MAX_LINE]; format_list_access(fmt, scratch);
        sb_printf(current, "    %s;\n", fmt);

        line = strtok(NULL, "\n");
    }

    if (auto_close) {
        while(SCOPE_DEPTH-- > 0) sb_append(current, "    }\n");
        if (IN_USER_FUNC) {
            sb_append(current, "}\n");
            IN_USER_FUNC = 0;
        }
    } else {
        // --- ERROR REPORTING FOR RAW MODE ---
        if (SCOPE_DEPTH > 0 || IN_USER_FUNC) {
            printf("\n[COMPILER ERROR] Syntax Error: Missing 'end' statement(s).\n");
            
            // Report the deepest unclosed block
            if (SCOPE_DEPTH > 0) {
                printf("  -> Unclosed block started at line %d\n", scope_lines[SCOPE_DEPTH-1]);
                if (SCOPE_DEPTH > 1) {
                    printf("     (Inside block from line %d)\n", scope_lines[SCOPE_DEPTH-2]);
                }
            } 
            // Report unclosed function
            else if (IN_USER_FUNC) {
                printf("  -> Unclosed function started at line %d\n", func_start_line);
            }
            
            free(source_code);
            sb_free(&func_buff);
            sb_free(&main_buff);
            return 1; 
        }
    }

    FILE* out = fopen("out.c", "w");
    if(!out) return 1;
    fprintf(out, "%s\n", STD_LIB);
    fwrite(func_buff.data, 1, func_buff.len, out);
    fprintf(out, "int main() {\n");
    fwrite(main_buff.data, 1, main_buff.len, out);
    fprintf(out, "    return 0;\n}\n");
    fclose(out);

    free(source_code);
    sb_free(&func_buff);
    sb_free(&main_buff);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: ./compiler <file> [mode]\nModes: optimized (default), raw, debug\n"); return 1; }
    char* f = argv[1]; 
    char* m = (argc >= 3) ? argv[2] : "optimized";
    
    int is_raw = (strcmp(m, "raw") == 0);
    int auto_close = !is_raw; 

    clock_t start = clock();
    
    if (compile_to_c(f, auto_close, 0) != 0) {
        printf("Compilation Aborted due to syntax errors.\n");
        return 1;
    }

    char cmd[2048];
    if (is_raw) snprintf(cmd, 2048, "gcc -O0 out.c -o program -lm");
    else if (strcmp(m, "debug") == 0) snprintf(cmd, 2048, "gcc -Ofast out.c -o program -lm && ./program");
    else snprintf(cmd, 2048, "gcc -Ofast -w out.c -o program -lm");

    int res = system(cmd);
    if (strcmp(m, "debug") != 0 && res == 0) printf("Compiled: ./program (%.3fs)\n", (double)(clock()-start)/CLOCKS_PER_SEC);
    return res;
}
