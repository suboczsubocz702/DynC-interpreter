#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>

const char *token_names[] = {
	"EOF","ID","NUM","STR","IF","ELSE","WHILE","FUNCTION","RETURN",
	"EQ","NE","LE","GE","AND","OR","ASSIGN","SEMICOLON","LPAREN",
	"RPAREN","LBRACE","RBRACE","LBRACKET","RBRACKET","COMMA","DOT",
	"PLUS","MINUS","MUL","DIV","LT","GT","NOT"
};

void error(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
	exit(1);
}

int has_dyn_extension(const char *filename) {
	const char *dot = strrchr(filename, '.');
	if (!dot) return 0;
	return strcmp(dot, ".dyn") == 0;
}

int validate_dyn_file(const char *filename) {
	const char *dot = strrchr(filename, '.');
	if (!dot) {
		fprintf(stderr, "Error: File '%s' has no extension. Expected .dyn\n", filename);
		return 0;
	}
	if (!has_dyn_extension(filename)) {
		fprintf(stderr, "Error: File '%s' has extension '%s'. Only .dyn files are supported\n", filename, dot);
		return 0;
	}
	FILE *f = fopen(filename, "rb");
	if (!f) {
		fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
		return 0;
	}
	fclose(f);
	return 1;
}

typedef struct Value Value;
typedef struct Object Object;
typedef struct AstNode AstNode;
typedef struct Env Env;
typedef struct Function Function;

Value *make_undefined(void);
Value *make_null(void);
Value *make_nan(void);
Value *make_number(double n);
Value *make_string(const char *s);
Value *make_object(void);
Value *make_function(char **params, int param_count, AstNode *body);
Object *object_new(void);
void free_value(Value *v);
char *to_string(Value *v);
Value *copy_value(Value *v);

typedef enum {
	VAL_UNDEFINED,
	VAL_NULL,
	VAL_NUMBER,
	VAL_STRING,
	VAL_OBJECT,
	VAL_FUNCTION,
	VAL_NAN
} ValueType;

typedef struct {
	char *key;
	Value *value;
} Field;

struct Object {
	Field *fields;
	int count;
	int capacity;
	struct Object *proto;
};

struct Function {
	char **params;
	int param_count;
	AstNode *body;
	Env *closure;
	Object *properties;
};

struct Value {
	ValueType type;
	union {
		double number;
		char *string;
		Object *object;
		Function *function;
	} data;
};

typedef struct {
	Object *obj;
	int ref_count;
} ObjectRef;

typedef struct {
	Function *func;
	int ref_count;
} FunctionRef;

ObjectRef *object_refs[1024];
int object_ref_count = 0;

FunctionRef *function_refs[1024];
int function_ref_count = 0;

ObjectRef *get_object_ref(Object *obj) {
	for (int i = 0; i < object_ref_count; i++) {
		if (object_refs[i]->obj == obj) {
			return object_refs[i];
		}
	}
	return NULL;
}

FunctionRef *get_function_ref(Function *func) {
	for (int i = 0; i < function_ref_count; i++) {
		if (function_refs[i]->func == func) {
			return function_refs[i];
		}
	}
	return NULL;
}

void retain_object(Object *obj) {
	ObjectRef *ref = get_object_ref(obj);
	if (ref) {
		ref->ref_count++;
	}
}

void release_object(Object *obj) {
	ObjectRef *ref = get_object_ref(obj);
	if (ref) {
		ref->ref_count--;
		if (ref->ref_count == 0) {
			for (int i = 0; i < object_ref_count; i++) {
				if (object_refs[i] == ref) {
					object_refs[i] = object_refs[--object_ref_count];
					break;
				}
			}
			for (int i = 0; i < obj->count; i++) {
				free(obj->fields[i].key);
				free_value(obj->fields[i].value);
			}
			free(obj->fields);
			free(obj);
			free(ref);
		}
	}
}

void retain_function(Function *func) {
	FunctionRef *ref = get_function_ref(func);
	if (ref) {
		ref->ref_count++;
	}
}

void release_function(Function *func) {
	FunctionRef *ref = get_function_ref(func);
	if (ref) {
		ref->ref_count--;
		if (ref->ref_count == 0) {
			for (int i = 0; i < function_ref_count; i++) {
				if (function_refs[i] == ref) {
					function_refs[i] = function_refs[--function_ref_count];
					break;
				}
			}
			if (func->properties) {
				for (int i = 0; i < func->properties->count; i++) {
					free(func->properties->fields[i].key);
					free_value(func->properties->fields[i].value);
				}
				free(func->properties->fields);
				free(func->properties);
			}
			for (int i = 0; i < func->param_count; i++) {
				free(func->params[i]);
			}
			free(func->params);
			free(func);
			free(ref);
		}
	}
}

Value *make_undefined(void) {
	Value *v = malloc(sizeof(Value));
	v->type = VAL_UNDEFINED;
	return v;
}

Value *make_null(void) {
	Value *v = malloc(sizeof(Value));
	v->type = VAL_NULL;
	return v;
}

Value *make_nan(void) {
	Value *v = malloc(sizeof(Value));
	v->type = VAL_NAN;
	return v;
}

Value *make_number(double n) {
	Value *v = malloc(sizeof(Value));
	v->type = VAL_NUMBER;
	v->data.number = n;
	return v;
}

Value *make_string(const char *s) {
	Value *v = malloc(sizeof(Value));
	v->type = VAL_STRING;
	v->data.string = strdup(s);
	return v;
}

Object *object_new(void) {
	Object *o = malloc(sizeof(Object));
	o->count = 0;
	o->capacity = 4;
	o->fields = malloc(sizeof(Field) * o->capacity);
	o->proto = NULL;
	return o;
}

Value *make_object(void) {
	Value *v = malloc(sizeof(Value));
	v->type = VAL_OBJECT;
	v->data.object = object_new();
	ObjectRef *ref = malloc(sizeof(ObjectRef));
	ref->obj = v->data.object;
	ref->ref_count = 1;
	object_refs[object_ref_count++] = ref;
	return v;
}

Value *make_function(char **params, int param_count, AstNode *body) {
	Value *v = malloc(sizeof(Value));
	v->type = VAL_FUNCTION;
	v->data.function = malloc(sizeof(Function));
	v->data.function->params = params;
	v->data.function->param_count = param_count;
	v->data.function->body = body;
	v->data.function->closure = NULL;
	v->data.function->properties = object_new();
	FunctionRef *ref = malloc(sizeof(FunctionRef));
	ref->func = v->data.function;
	ref->ref_count = 1;
	function_refs[function_ref_count++] = ref;
	ObjectRef *prop_ref = malloc(sizeof(ObjectRef));
	prop_ref->obj = v->data.function->properties;
	prop_ref->ref_count = 1;
	object_refs[object_ref_count++] = prop_ref;
	return v;
}

Value *copy_value(Value *v) {
	if (!v) return make_undefined();
	Value *copy = malloc(sizeof(Value));
	copy->type = v->type;
	switch (v->type) {
		case VAL_NUMBER:
			copy->data.number = v->data.number;
			break;
		case VAL_STRING:
			copy->data.string = strdup(v->data.string);
			break;
		case VAL_OBJECT:
			copy->data.object = v->data.object;
			retain_object(v->data.object);
			break;
		case VAL_FUNCTION:
			copy->data.function = v->data.function;
			retain_function(v->data.function);
			break;
		case VAL_UNDEFINED:
		case VAL_NULL:
		case VAL_NAN:
			break;
	}
	return copy;
}

void set_prototype(Object *o, Object *proto) {
	if (o) {
		o->proto = proto;
	}
}

Value *object_get_recursive(Object *o, const char *key) {
	if (!o) return make_undefined();
	for (int i = 0; i < o->count; i++) {
		if (strcmp(o->fields[i].key, key) == 0) {
			return o->fields[i].value;
		}
	}
	if (o->proto) {
		return object_get_recursive(o->proto, key);
	}
	return make_undefined();
}

Value *object_get(Object *o, const char *key) {
	Value *val = object_get_recursive(o, key);
	return copy_value(val);
}

void object_set(Object *o, const char *key, Value *v) {
	if (!o) return;
	for (int i = 0; i < o->count; i++) {
		if (strcmp(o->fields[i].key, key) == 0) {
			free_value(o->fields[i].value);
			o->fields[i].value = copy_value(v);
			return;
		}
	}
	if (o->count >= o->capacity) {
		o->capacity *= 2;
		o->fields = realloc(o->fields, sizeof(Field) * o->capacity);
	}
	o->fields[o->count].key = strdup(key);
	o->fields[o->count].value = copy_value(v);
	o->count++;
}

Value *object_get_index(Object *o, Value *index) {
	char key[256];
	if (index->type == VAL_NUMBER) {
		snprintf(key, sizeof(key), "%d", (int)index->data.number);
	} else if (index->type == VAL_STRING) {
		strncpy(key, index->data.string, sizeof(key) - 1);
		key[sizeof(key) - 1] = '\0';
	} else {
		strncpy(key, to_string(index), sizeof(key) - 1);
		key[sizeof(key) - 1] = '\0';
	}
	return object_get(o, key);
}

void object_set_index(Object *o, Value *index, Value *v) {
	char key[256];
	if (index->type == VAL_NUMBER) {
		snprintf(key, sizeof(key), "%d", (int)index->data.number);
	} else if (index->type == VAL_STRING) {
		strncpy(key, index->data.string, sizeof(key) - 1);
		key[sizeof(key) - 1] = '\0';
	} else {
		strncpy(key, to_string(index), sizeof(key) - 1);
		key[sizeof(key) - 1] = '\0';
	}
	object_set(o, key, v);
}

void free_value(Value *v) {
	if (!v) return;
	switch (v->type) {
		case VAL_STRING:
			free(v->data.string);
			break;
		case VAL_OBJECT:
			release_object(v->data.object);
			break;
		case VAL_FUNCTION:
			release_function(v->data.function);
			break;
		default:
			break;
	}
	free(v);
}

double to_number(Value *v) {
	if (!v) return 0;
	switch(v->type) {
		case VAL_NUMBER: return v->data.number;
		case VAL_STRING: {
					 char *end;
					 double d = strtod(v->data.string, &end);
					 if (*end != '\0') return 0;
					 return d;
				 }
		case VAL_UNDEFINED: return NAN;
		case VAL_NULL: return 0;
		case VAL_NAN: return NAN;
		case VAL_OBJECT: return 1;
		case VAL_FUNCTION: return 1;
	}
	return 0;
}

char *to_string(Value *v) {
	static char buf[256];
	if (!v) return "undefined";
	switch(v->type) {
		case VAL_STRING: return v->data.string;
		case VAL_NUMBER:
				 snprintf(buf, sizeof(buf), "%g", v->data.number);
				 return buf;
		case VAL_UNDEFINED: return "undefined";
		case VAL_NULL: return "null";
		case VAL_NAN: return "NaN";
		case VAL_OBJECT: return "[object]";
		case VAL_FUNCTION: return "[function]";
	}
	return "";
}

int truthy(Value *v) {
	if (!v) return 0;
	switch(v->type) {
		case VAL_UNDEFINED: return 0;
		case VAL_NULL: return 0;
		case VAL_NAN: return 0;
		case VAL_NUMBER: return v->data.number != 0 && !isnan(v->data.number);
		case VAL_STRING: return strlen(v->data.string) != 0;
		case VAL_OBJECT: return 1;
		case VAL_FUNCTION: return 1;
	}
	return 0;
}

int is_nan_value(Value *v) {
	if (!v) return 0;
	if (v->type == VAL_NAN) return 1;
	if (v->type == VAL_NUMBER && isnan(v->data.number)) return 1;
	return 0;
}

int values_equal(Value *a, Value *b) {
	if (!a || !b) return (a == b);
	if (is_nan_value(a) || is_nan_value(b)) return 0;
	if ((a->type == VAL_UNDEFINED && b->type == VAL_NULL) ||
			(a->type == VAL_NULL && b->type == VAL_UNDEFINED)) {
		return 1;
	}
	if (a->type == b->type) {
		switch (a->type) {
			case VAL_STRING:
				return strcmp(a->data.string, b->data.string) == 0;
			case VAL_NUMBER:
				return a->data.number == b->data.number;
			case VAL_OBJECT:
			case VAL_FUNCTION:
				return a->data.object == b->data.object;
			default:
				return 0;
		}
	}
	double da = to_number(a);
	double db = to_number(b);
	return da == db;
}

struct Env {
	struct Env *parent;
	char **names;
	Value **values;
	int count;
	int capacity;
	int is_function;
};

Env *global_env;

Env *env_new(Env *parent, int is_function) {
	Env *e = malloc(sizeof(Env));
	e->parent = parent;
	e->is_function = is_function;
	e->count = 0;
	e->capacity = 4;
	e->names = malloc(sizeof(char*) * e->capacity);
	e->values = malloc(sizeof(Value*) * e->capacity);
	return e;
}

void env_set(Env *e, const char *name, Value *v) {
	Env *cur = e;
	while (cur) {
		for (int i = 0; i < cur->count; i++) {
			if (strcmp(cur->names[i], name) == 0) {
				free_value(cur->values[i]);
				cur->values[i] = copy_value(v);
				return;
			}
		}
		cur = cur->parent;
	}
	e = global_env;
	if (e->count >= e->capacity) {
		e->capacity *= 2;
		e->names = realloc(e->names, sizeof(char*) * e->capacity);
		e->values = realloc(e->values, sizeof(Value*) * e->capacity);
	}
	e->names[e->count] = strdup(name);
	e->values[e->count] = copy_value(v);
	e->count++;
}

Value *env_get(Env *e, const char *name) {
	if (!e) return make_undefined();
	for (int i = 0; i < e->count; i++) {
		if (strcmp(e->names[i], name) == 0) {
			return e->values[i];
		}
	}
	if (e->parent) {
		return env_get(e->parent, name);
	}
	return make_undefined();
}

void env_free(Env *e) {
	if (!e) return;
	for (int i = 0; i < e->count; i++) {
		free(e->names[i]);
		free_value(e->values[i]);
	}
	free(e->names);
	free(e->values);
	free(e);
}

typedef enum {
	TOK_EOF,
	TOK_ID,
	TOK_NUM,
	TOK_STR,
	TOK_IF,
	TOK_ELSE,
	TOK_WHILE,
	TOK_FUNCTION,
	TOK_RETURN,
	TOK_EQ,
	TOK_NE,
	TOK_LE,
	TOK_GE,
	TOK_AND,
	TOK_OR,
	TOK_ASSIGN,
	TOK_SEMICOLON,
	TOK_LPAREN,
	TOK_RPAREN,
	TOK_LBRACE,
	TOK_RBRACE,
	TOK_LBRACKET,
	TOK_RBRACKET,
	TOK_COMMA,
	TOK_DOT,
	TOK_PLUS,
	TOK_MINUS,
	TOK_MUL,
	TOK_DIV,
	TOK_LT,
	TOK_GT,
	TOK_NOT
} TokenType;

typedef struct {
	TokenType type;
	union {
		char *sval;
		double nval;
	} value;
} Token;

char *src;
int src_pos;
Token current_token;

void next_token(void) {
	while (isspace(src[src_pos])) src_pos++;
	if (src[src_pos] == '/' && src[src_pos + 1] == '/') {
		src_pos += 2;
		while (src[src_pos] && src[src_pos] != '\n') src_pos++;
		if (src[src_pos] == '\n') src_pos++;
		next_token();
		return;
	}
	if (src[src_pos] == '/' && src[src_pos + 1] == '*') {
		src_pos += 2;
		while (src[src_pos] && !(src[src_pos] == '*' && src[src_pos + 1] == '/')) {
			src_pos++;
		}
		if (src[src_pos]) {
			src_pos += 2;
		}
		next_token();
		return;
	}
	if (src[src_pos] == '\0') {
		current_token.type = TOK_EOF;
		return;
	}
	if (isdigit(src[src_pos])) {
		char *end;
		current_token.value.nval = strtod(src + src_pos, &end);
		src_pos = end - src;
		current_token.type = TOK_NUM;
		return;
	}
	if (isalpha(src[src_pos]) || src[src_pos] == '_') {
		int start = src_pos;
		while (isalnum(src[src_pos]) || src[src_pos] == '_') src_pos++;
		int len = src_pos - start;
		char *ident = malloc(len + 1);
		strncpy(ident, src + start, len);
		ident[len] = '\0';
		if (strcmp(ident, "if") == 0) {
			current_token.type = TOK_IF;
		} else if (strcmp(ident, "else") == 0) {
			current_token.type = TOK_ELSE;
		} else if (strcmp(ident, "while") == 0) {
			current_token.type = TOK_WHILE;
		} else if (strcmp(ident, "function") == 0) {
			current_token.type = TOK_FUNCTION;
		} else if (strcmp(ident, "return") == 0) {
			current_token.type = TOK_RETURN;
		} else {
			current_token.type = TOK_ID;
			current_token.value.sval = ident;
			return;
		}
		free(ident);
		return;
	}
	if (src[src_pos] == '"') {
		src_pos++;
		char buffer[4096];
		int len = 0;
		while (src[src_pos] != '"') {
			if (src[src_pos] == '\0')
				error("Unterminated string");
			if (src[src_pos] == '\\') {
				src_pos++;
				switch (src[src_pos]) {
					case 'n': buffer[len++] = '\n'; break;
					case 't': buffer[len++] = '\t'; break;
					case 'r': buffer[len++] = '\r'; break;
					case '\\': buffer[len++] = '\\'; break;
					case '"': buffer[len++] = '"'; break;
					default: buffer[len++] = src[src_pos]; break;
				}
				src_pos++;
				continue;
			}
			buffer[len++] = src[src_pos++];
		}
		buffer[len] = '\0';
		src_pos++;
		current_token.type = TOK_STR;
		current_token.value.sval = strdup(buffer);
		return;
	}
	if (src[src_pos] == '=' && src[src_pos + 1] == '=') {
		src_pos += 2;
		current_token.type = TOK_EQ;
		return;
	}
	if (src[src_pos] == '!' && src[src_pos + 1] == '=') {
		src_pos += 2;
		current_token.type = TOK_NE;
		return;
	}
	if (src[src_pos] == '<' && src[src_pos + 1] == '=') {
		src_pos += 2;
		current_token.type = TOK_LE;
		return;
	}
	if (src[src_pos] == '>' && src[src_pos + 1] == '=') {
		src_pos += 2;
		current_token.type = TOK_GE;
		return;
	}
	if (src[src_pos] == '&' && src[src_pos + 1] == '&') {
		src_pos += 2;
		current_token.type = TOK_AND;
		return;
	}
	if (src[src_pos] == '|' && src[src_pos + 1] == '|') {
		src_pos += 2;
		current_token.type = TOK_OR;
		return;
	}
	char c = src[src_pos++];
	switch (c) {
		case '=': current_token.type = TOK_ASSIGN; break;
		case ';': current_token.type = TOK_SEMICOLON; break;
		case '(': current_token.type = TOK_LPAREN; break;
		case ')': current_token.type = TOK_RPAREN; break;
		case '{': current_token.type = TOK_LBRACE; break;
		case '}': current_token.type = TOK_RBRACE; break;
		case '[': current_token.type = TOK_LBRACKET; break;
		case ']': current_token.type = TOK_RBRACKET; break;
		case ',': current_token.type = TOK_COMMA; break;
		case '.': current_token.type = TOK_DOT; break;
		case '+': current_token.type = TOK_PLUS; break;
		case '-': current_token.type = TOK_MINUS; break;
		case '*': current_token.type = TOK_MUL; break;
		case '/': current_token.type = TOK_DIV; break;
		case '<': current_token.type = TOK_LT; break;
		case '>': current_token.type = TOK_GT; break;
		case '!': current_token.type = TOK_NOT; break;
		default: error("Unexpected character: %c", c);
	}
}

void expect(TokenType type) {
	if (current_token.type != type) {
		error("Expected %s, got %s", token_names[type], token_names[current_token.type]);
	}
	next_token();
}

typedef enum {
	AST_NUMBER,
	AST_STRING,
	AST_IDENT,
	AST_ASSIGN,
	AST_ASSIGN_MEMBER_DOT,
	AST_ASSIGN_MEMBER_INDEX,
	AST_BINARY,
	AST_UNARY,
	AST_CALL,
	AST_IF,
	AST_WHILE,
	AST_RETURN,
	AST_BLOCK,
	AST_FUNCTION,
	AST_MEMBER_DOT,
	AST_MEMBER_INDEX
} AstType;

struct AstNode {
	AstType type;
	union {
		struct { double value; } number;
		struct { char *value; } string;
		struct { char *name; } ident;
		struct { char *name; AstNode *value; } assign;
		struct { AstNode *object; char *property; AstNode *value; } assign_member_dot;
		struct { AstNode *object; AstNode *index; AstNode *value; } assign_member_index;
		struct { int op; AstNode *left; AstNode *right; } binary;
		struct { int op; AstNode *operand; } unary;
		struct { AstNode *callee; AstNode **args; int arg_count; } call;
		struct { AstNode *cond; AstNode *then; AstNode *else_; } if_stmt;
		struct { AstNode *cond; AstNode *body; } while_stmt;
		struct { AstNode *value; } return_stmt;
		struct { AstNode **stmts; int count; } block;
		struct { char **params; int param_count; AstNode *body; char *name; } function;
		struct { AstNode *object; char *property; } member_dot;
		struct { AstNode *object; AstNode *index; } member_index;
	} data;
};

AstNode *ast_new(AstType type) {
	AstNode *node = malloc(sizeof(AstNode));
	node->type = type;
	return node;
}

void ast_free(AstNode *node) {
	if (!node) return;
	switch (node->type) {
		case AST_NUMBER:
		case AST_STRING:
		case AST_IDENT:
			break;
		case AST_ASSIGN:
			free(node->data.assign.name);
			ast_free(node->data.assign.value);
			break;
		case AST_ASSIGN_MEMBER_DOT:
			ast_free(node->data.assign_member_dot.object);
			free(node->data.assign_member_dot.property);
			ast_free(node->data.assign_member_dot.value);
			break;
		case AST_ASSIGN_MEMBER_INDEX:
			ast_free(node->data.assign_member_index.object);
			ast_free(node->data.assign_member_index.index);
			ast_free(node->data.assign_member_index.value);
			break;
		case AST_BINARY:
			ast_free(node->data.binary.left);
			ast_free(node->data.binary.right);
			break;
		case AST_UNARY:
			ast_free(node->data.unary.operand);
			break;
		case AST_CALL:
			ast_free(node->data.call.callee);
			for (int i = 0; i < node->data.call.arg_count; i++) {
				ast_free(node->data.call.args[i]);
			}
			free(node->data.call.args);
			break;
		case AST_IF:
			ast_free(node->data.if_stmt.cond);
			ast_free(node->data.if_stmt.then);
			if (node->data.if_stmt.else_)
				ast_free(node->data.if_stmt.else_);
			break;
		case AST_WHILE:
			ast_free(node->data.while_stmt.cond);
			ast_free(node->data.while_stmt.body);
			break;
		case AST_RETURN:
			if (node->data.return_stmt.value)
				ast_free(node->data.return_stmt.value);
			break;
		case AST_BLOCK:
			for (int i = 0; i < node->data.block.count; i++) {
				ast_free(node->data.block.stmts[i]);
			}
			free(node->data.block.stmts);
			break;
		case AST_FUNCTION:
			for (int i = 0; i < node->data.function.param_count; i++) {
				free(node->data.function.params[i]);
			}
			free(node->data.function.params);
			free(node->data.function.name);
			ast_free(node->data.function.body);
			break;
		case AST_MEMBER_DOT:
			ast_free(node->data.member_dot.object);
			free(node->data.member_dot.property);
			break;
		case AST_MEMBER_INDEX:
			ast_free(node->data.member_index.object);
			ast_free(node->data.member_index.index);
			break;
	}
	free(node);
}

typedef struct {
	char *name;
	AstNode *func_node;
} FunctionDef;

FunctionDef functions[1024];
int function_count = 0;

AstNode *parse_expression(void);
AstNode *parse_assignment(void);
AstNode *parse_statement(void);
AstNode *parse_function_definition(int with_keyword);
AstNode *parse_program(void);

void collect_functions(AstNode *node) {
	if (!node) return;
	if (node->type == AST_FUNCTION && node->data.function.name) {
		functions[function_count].name = strdup(node->data.function.name);
		functions[function_count].func_node = node;
		function_count++;
	}
	switch (node->type) {
		case AST_BLOCK:
			for (int i = 0; i < node->data.block.count; i++) {
				collect_functions(node->data.block.stmts[i]);
			}
			break;
		case AST_IF:
			collect_functions(node->data.if_stmt.cond);
			collect_functions(node->data.if_stmt.then);
			if (node->data.if_stmt.else_)
				collect_functions(node->data.if_stmt.else_);
			break;
		case AST_WHILE:
			collect_functions(node->data.while_stmt.cond);
			collect_functions(node->data.while_stmt.body);
			break;
		case AST_FUNCTION:
			collect_functions(node->data.function.body);
			break;
		default:
			break;
	}
}

AstNode *parse_primary(void) {
	AstNode *node = NULL;
	switch (current_token.type) {
		case TOK_NUM:
			node = ast_new(AST_NUMBER);
			node->data.number.value = current_token.value.nval;
			next_token();
			break;
		case TOK_STR:
			node = ast_new(AST_STRING);
			node->data.string.value = current_token.value.sval;
			next_token();
			break;
		case TOK_ID: {
				     char *name = current_token.value.sval;
				     next_token();
				     node = ast_new(AST_IDENT);
				     node->data.ident.name = name;
				     break;
			     }
		case TOK_LPAREN:
			     next_token();
			     node = parse_expression();
			     expect(TOK_RPAREN);
			     break;
		default:
			     error("Unexpected token in primary expression: %s", token_names[current_token.type]);
	}
	return node;
}

AstNode *parse_member(AstNode *left) {
	while (current_token.type == TOK_DOT || current_token.type == TOK_LBRACKET) {
		if (current_token.type == TOK_DOT) {
			next_token();
			if (current_token.type != TOK_ID) {
				error("Expected property name after '.', got %s", token_names[current_token.type]);
			}
			char *prop = current_token.value.sval;
			next_token();
			AstNode *member = ast_new(AST_MEMBER_DOT);
			member->data.member_dot.object = left;
			member->data.member_dot.property = prop;
			left = member;
		} else {
			next_token();
			AstNode *index = parse_expression();
			expect(TOK_RBRACKET);
			AstNode *member = ast_new(AST_MEMBER_INDEX);
			member->data.member_index.object = left;
			member->data.member_index.index = index;
			left = member;
		}
	}
	return left;
}

AstNode *parse_call(void) {
	AstNode *node = parse_primary();
	node = parse_member(node);
	while (current_token.type == TOK_LPAREN) {
		next_token();
		AstNode **args = NULL;
		int arg_count = 0;
		if (current_token.type != TOK_RPAREN) {
			args = malloc(sizeof(AstNode*));
			args[0] = parse_expression();
			arg_count = 1;
			while (current_token.type == TOK_COMMA) {
				next_token();
				args = realloc(args, sizeof(AstNode*) * (arg_count + 1));
				args[arg_count++] = parse_expression();
			}
		}
		expect(TOK_RPAREN);
		AstNode *call = ast_new(AST_CALL);
		call->data.call.callee = node;
		call->data.call.args = args;
		call->data.call.arg_count = arg_count;
		node = call;
		node = parse_member(node);
	}
	return node;
}

AstNode *parse_unary(void) {
	if (current_token.type == TOK_MINUS || current_token.type == TOK_NOT) {
		int op = current_token.type;
		next_token();
		AstNode *node = ast_new(AST_UNARY);
		node->data.unary.op = op;
		node->data.unary.operand = parse_unary();
		return node;
	}
	return parse_call();
}

AstNode *parse_mul(void) {
	AstNode *node = parse_unary();
	while (current_token.type == TOK_MUL || current_token.type == TOK_DIV) {
		int op = current_token.type;
		next_token();
		AstNode *right = parse_unary();
		AstNode *binary = ast_new(AST_BINARY);
		binary->data.binary.op = op;
		binary->data.binary.left = node;
		binary->data.binary.right = right;
		node = binary;
	}
	return node;
}

AstNode *parse_add(void) {
	AstNode *node = parse_mul();
	while (current_token.type == TOK_PLUS || current_token.type == TOK_MINUS) {
		int op = current_token.type;
		next_token();
		AstNode *right = parse_mul();
		AstNode *binary = ast_new(AST_BINARY);
		binary->data.binary.op = op;
		binary->data.binary.left = node;
		binary->data.binary.right = right;
		node = binary;
	}
	return node;
}

AstNode *parse_relational(void) {
	AstNode *node = parse_add();
	while (current_token.type == TOK_LT || current_token.type == TOK_GT ||
			current_token.type == TOK_LE || current_token.type == TOK_GE) {
		int op = current_token.type;
		next_token();
		AstNode *right = parse_add();
		AstNode *binary = ast_new(AST_BINARY);
		binary->data.binary.op = op;
		binary->data.binary.left = node;
		binary->data.binary.right = right;
		node = binary;
	}
	return node;
}

AstNode *parse_equality(void) {
	AstNode *node = parse_relational();
	while (current_token.type == TOK_EQ || current_token.type == TOK_NE) {
		int op = current_token.type;
		next_token();
		AstNode *right = parse_relational();
		AstNode *binary = ast_new(AST_BINARY);
		binary->data.binary.op = op;
		binary->data.binary.left = node;
		binary->data.binary.right = right;
		node = binary;
	}
	return node;
}

AstNode *parse_and(void) {
	AstNode *node = parse_equality();
	while (current_token.type == TOK_AND) {
		int op = current_token.type;
		next_token();
		AstNode *right = parse_equality();
		AstNode *binary = ast_new(AST_BINARY);
		binary->data.binary.op = op;
		binary->data.binary.left = node;
		binary->data.binary.right = right;
		node = binary;
	}
	return node;
}

AstNode *parse_or(void) {
	AstNode *node = parse_and();
	while (current_token.type == TOK_OR) {
		int op = current_token.type;
		next_token();
		AstNode *right = parse_and();
		AstNode *binary = ast_new(AST_BINARY);
		binary->data.binary.op = op;
		binary->data.binary.left = node;
		binary->data.binary.right = right;
		node = binary;
	}
	return node;
}

AstNode *parse_assignment(void) {
	AstNode *node = parse_or();
	if (current_token.type == TOK_ASSIGN) {
		next_token();
		AstNode *value = parse_assignment();
		if (node->type == AST_IDENT) {
			AstNode *assign = ast_new(AST_ASSIGN);
			assign->data.assign.name = node->data.ident.name;
			assign->data.assign.value = value;
			ast_free(node);
			return assign;
		} else if (node->type == AST_MEMBER_DOT) {
			AstNode *assign = ast_new(AST_ASSIGN_MEMBER_DOT);
			assign->data.assign_member_dot.object = node->data.member_dot.object;
			assign->data.assign_member_dot.property = node->data.member_dot.property;
			assign->data.assign_member_dot.value = value;
			free(node);
			return assign;
		} else if (node->type == AST_MEMBER_INDEX) {
			AstNode *assign = ast_new(AST_ASSIGN_MEMBER_INDEX);
			assign->data.assign_member_index.object = node->data.member_index.object;
			assign->data.assign_member_index.index = node->data.member_index.index;
			assign->data.assign_member_index.value = value;
			free(node);
			return assign;
		} else {
			error("Invalid left-hand side in assignment");
		}
	}
	return node;
}

AstNode *parse_expression(void) {
	return parse_assignment();
}

AstNode *parse_function_definition(int with_keyword) {
	if (with_keyword) {
		expect(TOK_FUNCTION);
	}
	if (current_token.type != TOK_ID) {
		error("Expected function name, got %s", token_names[current_token.type]);
	}
	char *name = current_token.value.sval;
	next_token();
	if (current_token.type != TOK_LPAREN) {
		error("Expected '(' after function name, got %s", token_names[current_token.type]);
	}
	next_token();
	char **params = NULL;
	int param_count = 0;
	if (current_token.type != TOK_RPAREN) {
		if (current_token.type != TOK_ID) {
			error("Expected parameter name, got %s", token_names[current_token.type]);
		}
		params = malloc(sizeof(char*));
		params[0] = current_token.value.sval;
		param_count = 1;
		next_token();
		while (current_token.type == TOK_COMMA) {
			next_token();
			if (current_token.type != TOK_ID) {
				error("Expected parameter name after ',', got %s", token_names[current_token.type]);
			}
			params = realloc(params, sizeof(char*) * (param_count + 1));
			params[param_count++] = current_token.value.sval;
			next_token();
		}
	}
	if (current_token.type != TOK_RPAREN) {
		error("Expected ')' after parameters, got %s", token_names[current_token.type]);
	}
	next_token();
	if (current_token.type != TOK_LBRACE) {
		error("Expected '{' for function body, got %s", token_names[current_token.type]);
	}
	next_token();
	AstNode **stmts = NULL;
	int stmt_count = 0;
	while (current_token.type != TOK_RBRACE && current_token.type != TOK_EOF) {
		stmts = realloc(stmts, sizeof(AstNode*) * (stmt_count + 1));
		stmts[stmt_count++] = parse_statement();
	}
	if (current_token.type != TOK_RBRACE) {
		error("Expected '}' at end of function body, got %s", token_names[current_token.type]);
	}
	next_token();
	AstNode *body = ast_new(AST_BLOCK);
	body->data.block.stmts = stmts;
	body->data.block.count = stmt_count;
	AstNode *node = ast_new(AST_FUNCTION);
	node->data.function.params = params;
	node->data.function.param_count = param_count;
	node->data.function.body = body;
	node->data.function.name = name;
	return node;
}

AstNode *parse_statement(void) {
	AstNode *node = NULL;
	switch (current_token.type) {
		case TOK_IF: {
				     next_token();
				     expect(TOK_LPAREN);
				     AstNode *cond = parse_expression();
				     expect(TOK_RPAREN);
				     AstNode *then_branch = parse_statement();
				     AstNode *else_branch = NULL;
				     if (current_token.type == TOK_ELSE) {
					     next_token();
					     else_branch = parse_statement();
				     }
				     node = ast_new(AST_IF);
				     node->data.if_stmt.cond = cond;
				     node->data.if_stmt.then = then_branch;
				     node->data.if_stmt.else_ = else_branch;
				     break;
			     }
		case TOK_WHILE: {
					next_token();
					expect(TOK_LPAREN);
					AstNode *cond = parse_expression();
					expect(TOK_RPAREN);
					AstNode *body = parse_statement();
					node = ast_new(AST_WHILE);
					node->data.while_stmt.cond = cond;
					node->data.while_stmt.body = body;
					break;
				}
		case TOK_RETURN: {
					 next_token();
					 AstNode *value = NULL;
					 if (current_token.type != TOK_SEMICOLON) {
						 value = parse_expression();
					 }
					 expect(TOK_SEMICOLON);
					 node = ast_new(AST_RETURN);
					 node->data.return_stmt.value = value;
					 break;
				 }
		case TOK_LBRACE: {
					 next_token();
					 AstNode **stmts = NULL;
					 int count = 0;
					 while (current_token.type != TOK_RBRACE && current_token.type != TOK_EOF) {
						 stmts = realloc(stmts, sizeof(AstNode*) * (count + 1));
						 stmts[count++] = parse_statement();
					 }
					 expect(TOK_RBRACE);
					 node = ast_new(AST_BLOCK);
					 node->data.block.stmts = stmts;
					 node->data.block.count = count;
					 break;
				 }
		case TOK_FUNCTION:
				 node = parse_function_definition(1);
				 break;
		case TOK_ID: {
				     int peek = src_pos;
				     while (isalnum(src[peek]) || src[peek] == '_')
					     peek++;
				     while (isspace(src[peek]))
					     peek++;
				     if (src[peek] == '(') {
					     int depth = 1;
					     peek++;
					     while (src[peek] && depth > 0) {
						     if (src[peek] == '(') depth++;
						     else if (src[peek] == ')') depth--;
						     peek++;
					     }
					     while (isspace(src[peek]))
						     peek++;
					     if (src[peek] == '{') {
						     node = parse_function_definition(0);
						     break;
					     }
				     }
				     node = parse_expression();
				     expect(TOK_SEMICOLON);
				     break;
			     }
		default:
			     node = parse_expression();
			     expect(TOK_SEMICOLON);
			     break;
	}
	return node;
}

AstNode *parse_program(void) {
	next_token();
	AstNode **stmts = NULL;
	int count = 0;
	while (current_token.type != TOK_EOF) {
		stmts = realloc(stmts, sizeof(AstNode*) * (count + 1));
		stmts[count++] = parse_statement();
	}
	AstNode *program = ast_new(AST_BLOCK);
	program->data.block.stmts = stmts;
	program->data.block.count = count;
	function_count = 0;
	collect_functions(program);
	return program;
}

Value *builtin_printf(Value **args, int arg_count) {
	if (arg_count == 0) {
		return make_undefined();
	}
	if (args[0]->type == VAL_STRING) {
		const char *fmt = args[0]->data.string;
		int arg_index = 1;
		for (int i = 0; fmt[i]; i++) {
			if (fmt[i] == '%' && fmt[i+1]) {
				i++;
				if (arg_index >= arg_count) {
					putchar('?');
					continue;
				}
				Value *v = args[arg_index++];
				switch (fmt[i]) {
					case 'd': printf("%d", (int)to_number(v)); break;
					case 'f': printf("%f", to_number(v)); break;
					case 's': printf("%s", to_string(v)); break;
					case '%': putchar('%'); break;
					default: putchar('%'); putchar(fmt[i]); break;
				}
			} else {
				putchar(fmt[i]);
			}
		}
	} else {
		for (int i = 0; i < arg_count; i++) {
			printf("%s", to_string(args[i]));
		}
	}
	return make_undefined();
}

Value *builtin_make_object(Value **args, int arg_count) {
	(void)args;
	(void)arg_count;
	return make_object();
}

Value *builtin_set_prototype(Value **args, int arg_count) {
	if (arg_count >= 2) {
		if (args[0]->type == VAL_OBJECT && args[1]->type == VAL_OBJECT) {
			args[0]->data.object->proto = args[1]->data.object;
			return make_null();
		}
	}
	return make_undefined();
}

typedef struct {
	Env *env;
	int returning;
} EvalContext;

Value *evaluate(AstNode *node, EvalContext *ctx) {
	if (!node) return make_undefined();
	Value *result = NULL;
	switch (node->type) {
		case AST_NUMBER:
			result = make_number(node->data.number.value);
			break;
		case AST_STRING:
			result = make_string(node->data.string.value);
			break;
		case AST_IDENT: {
					Value *val = env_get(ctx->env, node->data.ident.name);
					result = copy_value(val);
					break;
				}
		case AST_ASSIGN: {
					 Value *val = evaluate(node->data.assign.value, ctx);
					 env_set(ctx->env, node->data.assign.name, val);
					 result = copy_value(val);
					 free_value(val);
					 break;
				 }
		case AST_ASSIGN_MEMBER_DOT: {
						    Value *obj = evaluate(node->data.assign_member_dot.object, ctx);
						    Value *val = evaluate(node->data.assign_member_dot.value, ctx);
						    if (obj->type == VAL_OBJECT) {
							    object_set(obj->data.object, node->data.assign_member_dot.property, val);
							    result = copy_value(val);
						    } else if (obj->type == VAL_FUNCTION) {
							    object_set(obj->data.function->properties, node->data.assign_member_dot.property, val);
							    result = copy_value(val);
						    } else {
							    result = make_undefined();
						    }
						    free_value(obj);
						    free_value(val);
						    break;
					    }
		case AST_ASSIGN_MEMBER_INDEX: {
						      Value *obj = evaluate(node->data.assign_member_index.object, ctx);
						      Value *idx = evaluate(node->data.assign_member_index.index, ctx);
						      Value *val = evaluate(node->data.assign_member_index.value, ctx);
						      if (obj->type == VAL_OBJECT) {
							      object_set_index(obj->data.object, idx, val);
							      result = copy_value(val);
						      } else if (obj->type == VAL_FUNCTION) {
							      char key[256];
							      if (idx->type == VAL_NUMBER) {
								      snprintf(key, sizeof(key), "%g", idx->data.number);
							      } else {
								      strncpy(key, to_string(idx), sizeof(key) - 1);
								      key[sizeof(key) - 1] = '\0';
							      }
							      object_set(obj->data.function->properties, key, val);
							      result = copy_value(val);
						      } else {
							      result = make_undefined();
						      }
						      free_value(obj);
						      free_value(idx);
						      free_value(val);
						      break;
					      }
		case AST_BINARY: {
					 Value *left = evaluate(node->data.binary.left, ctx);
					 Value *right = evaluate(node->data.binary.right, ctx);
					 switch (node->data.binary.op) {
						 case TOK_PLUS:
							 if (left->type == VAL_STRING || right->type == VAL_STRING) {
								 char buf[1024];
								 snprintf(buf, sizeof(buf), "%s%s", to_string(left), to_string(right));
								 result = make_string(buf);
							 } else {
								 double a = to_number(left);
								 double b = to_number(right);
								 if (isnan(a) || isnan(b)) {
									 result = make_nan();
								 } else {
									 result = make_number(a + b);
								 }
							 }
							 break;
						 case TOK_MINUS: {
									 double a = to_number(left);
									 double b = to_number(right);
									 if (isnan(a) || isnan(b)) {
										 result = make_nan();
									 } else {
										 result = make_number(a - b);
									 }
									 break;
								 }
						 case TOK_MUL: {
								       double a = to_number(left);
								       double b = to_number(right);
								       if (isnan(a) || isnan(b)) {
									       result = make_nan();
								       } else {
									       result = make_number(a * b);
								       }
								       break;
							       }
						 case TOK_DIV: {
								       double a = to_number(left);
								       double b = to_number(right);
								       if (isnan(a) || isnan(b) || b == 0) {
									       result = make_nan();
								       } else {
									       result = make_number(a / b);
								       }
								       break;
							       }
						 case TOK_LT:
							       result = make_number(to_number(left) < to_number(right));
							       break;
						 case TOK_GT:
							       result = make_number(to_number(left) > to_number(right));
							       break;
						 case TOK_LE:
							       result = make_number(to_number(left) <= to_number(right));
							       break;
						 case TOK_GE:
							       result = make_number(to_number(left) >= to_number(right));
							       break;
						 case TOK_EQ:
							       result = make_number(values_equal(left, right));
							       break;
						 case TOK_NE:
							       result = make_number(!values_equal(left, right));
							       break;
						 case TOK_AND:
							       result = make_number(truthy(left) && truthy(right));
							       break;
						 case TOK_OR:
							       result = make_number(truthy(left) || truthy(right));
							       break;
					 }
					 free_value(left);
					 free_value(right);
					 break;
				 }
		case AST_UNARY: {
					Value *operand = evaluate(node->data.unary.operand, ctx);
					switch (node->data.unary.op) {
						case TOK_MINUS: {
									double d = to_number(operand);
									if (isnan(d)) {
										result = make_nan();
									} else {
										result = make_number(-d);
									}
									break;
								}
						case TOK_NOT:
								result = make_number(!truthy(operand));
								break;
					}
					free_value(operand);
					break;
				}
		case AST_CALL: {
				       Value *callee = evaluate(node->data.call.callee, ctx);
				       if (node->data.call.callee->type == AST_IDENT) {
					       const char *name = node->data.call.callee->data.ident.name;
					       if (strcmp(name, "printf") == 0) {
						       Value **args = malloc(sizeof(Value*) * node->data.call.arg_count);
						       for (int i = 0; i < node->data.call.arg_count; i++) {
							       args[i] = evaluate(node->data.call.args[i], ctx);
						       }
						       result = builtin_printf(args, node->data.call.arg_count);
						       for (int i = 0; i < node->data.call.arg_count; i++) {
							       free_value(args[i]);
						       }
						       free(args);
						       free_value(callee);
						       break;
					       }
					       if (strcmp(name, "make_object") == 0) {
						       Value **args = malloc(sizeof(Value*) * node->data.call.arg_count);
						       for (int i = 0; i < node->data.call.arg_count; i++) {
							       args[i] = evaluate(node->data.call.args[i], ctx);
						       }
						       result = builtin_make_object(args, node->data.call.arg_count);
						       for (int i = 0; i < node->data.call.arg_count; i++) {
							       free_value(args[i]);
						       }
						       free(args);
						       free_value(callee);
						       break;
					       }
					       if (strcmp(name, "set_prototype") == 0) {
						       Value **args = malloc(sizeof(Value*) * node->data.call.arg_count);
						       for (int i = 0; i < node->data.call.arg_count; i++) {
							       args[i] = evaluate(node->data.call.args[i], ctx);
						       }
						       result = builtin_set_prototype(args, node->data.call.arg_count);
						       for (int i = 0; i < node->data.call.arg_count; i++) {
							       free_value(args[i]);
						       }
						       free(args);
						       free_value(callee);
						       break;
					       }
				       }
				       if (callee->type != VAL_FUNCTION) {
					       result = make_undefined();
					       free_value(callee);
					       break;
				       }
				       Function *func = callee->data.function;
				       Env *func_env = env_new(func->closure, 1);
				       for (int i = 0; i < node->data.call.arg_count; i++) {
					       Value *arg_val = evaluate(node->data.call.args[i], ctx);
					       if (i < func->param_count) {
						       env_set(func_env, func->params[i], arg_val);
					       } else {
						       free_value(arg_val);
					       }
				       }
				       for (int i = node->data.call.arg_count; i < func->param_count; i++) {
					       env_set(func_env, func->params[i], make_undefined());
				       }
				       EvalContext func_ctx = { func_env, 0 };
				       Value *return_val = evaluate(func->body, &func_ctx);
				       if (func_ctx.returning) {
					       result = return_val;
				       } else {
					       result = make_undefined();
					       if (return_val) free_value(return_val);
				       }
				       env_free(func_env);
				       free_value(callee);
				       break;
			       }
		case AST_IF: {
				     Value *cond = evaluate(node->data.if_stmt.cond, ctx);
				     if (truthy(cond)) {
					     result = evaluate(node->data.if_stmt.then, ctx);
					     free_value(cond);
					     if (ctx->returning) {
						     return result;
					     }
				     } else if (node->data.if_stmt.else_) {
					     result = evaluate(node->data.if_stmt.else_, ctx);
					     free_value(cond);
					     if (ctx->returning) {
						     return result;
					     }
				     } else {
					     result = make_undefined();
					     free_value(cond);
				     }
				     break;
			     }
		case AST_WHILE: {
					result = make_undefined();
					while (1) {
						Value *cond = evaluate(node->data.while_stmt.cond, ctx);
						int cond_truthy = truthy(cond);
						free_value(cond);
						if (!cond_truthy) break;
						Value *body_result = evaluate(node->data.while_stmt.body, ctx);
						if (ctx->returning) {
							return body_result;
						}
						free_value(body_result);
					}
					break;
				}
		case AST_RETURN: {
					 if (node->data.return_stmt.value) {
						 result = evaluate(node->data.return_stmt.value, ctx);
					 } else {
						 result = make_undefined();
					 }
					 ctx->returning = 1;
					 return result;
				 }
		case AST_BLOCK: {
					result = make_undefined();
					for (int i = 0; i < node->data.block.count; i++) {
						if (node->data.block.stmts[i]->type == AST_FUNCTION) {
							continue;
						}
						if (result) free_value(result);
						result = evaluate(node->data.block.stmts[i], ctx);
						if (ctx->returning) {
							return result;
						}
					}
					break;
				}
		case AST_FUNCTION: {
					   Value *func = make_function(node->data.function.params, node->data.function.param_count, node->data.function.body);
					   func->data.function->closure = ctx->env;
					   result = func;
					   break;
				   }
		case AST_MEMBER_DOT: {
					     Value *obj = evaluate(node->data.member_dot.object, ctx);
					     if (obj->type == VAL_OBJECT) {
						     result = object_get(obj->data.object, node->data.member_dot.property);
					     } else if (obj->type == VAL_FUNCTION) {
						     result = object_get(obj->data.function->properties, node->data.member_dot.property);
					     } else {
						     result = make_undefined();
					     }
					     free_value(obj);
					     break;
				     }
		case AST_MEMBER_INDEX: {
					       Value *obj = evaluate(node->data.member_index.object, ctx);
					       Value *idx = evaluate(node->data.member_index.index, ctx);
					       if (obj->type == VAL_OBJECT) {
						       result = object_get_index(obj->data.object, idx);
					       } else if (obj->type == VAL_FUNCTION) {
						       char key[256];
						       if (idx->type == VAL_NUMBER) {
							       snprintf(key, sizeof(key), "%g", idx->data.number);
						       } else {
							       strncpy(key, to_string(idx), sizeof(key) - 1);
							       key[sizeof(key) - 1] = '\0';
						       }
						       result = object_get(obj->data.function->properties, key);
					       } else if (obj->type == VAL_STRING) {
						       double d = to_number(idx);
						       if (!isnan(d) && d >= 0 && d < strlen(obj->data.string)) {
							       char s[2] = { obj->data.string[(int)d], '\0' };
							       result = make_string(s);
						       } else {
							       result = make_undefined();
						       }
					       } else {
						       result = make_undefined();
					       }
					       free_value(obj);
					       free_value(idx);
					       break;
				       }
	}
	return result;
}

void register_builtins(Env *env) {
	(void)env;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <file.dyn>\n", argv[0]);
		return 1;
	}
	if (!validate_dyn_file(argv[1])) {
		return 1;
	}
	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		error("Cannot open file: %s", argv[1]);
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	rewind(f);
	src = malloc(size + 1);
	if (!src) {
		error("Out of memory reading file");
	}
	size_t read = fread(src, 1, size, f);
	if (read != size) {
		error("Failed to read entire file");
	}
	src[size] = '\0';
	fclose(f);
	src_pos = 0;
	AstNode *program = parse_program();
	global_env = env_new(NULL, 0);
	register_builtins(global_env);
	for (int i = 0; i < function_count; i++) {
		AstNode *func_node = functions[i].func_node;
		Value *func_val = make_function(func_node->data.function.params, func_node->data.function.param_count, func_node->data.function.body);
		func_val->data.function->closure = global_env;
		env_set(global_env, functions[i].name, func_val);
	}
	EvalContext ctx = { global_env, 0 };
	Value *result = evaluate(program, &ctx);
	free_value(result);
	env_free(global_env);
	for (int i = 0; i < function_count; i++) {
		free(functions[i].name);
	}
	ast_free(program);
	free(src);
	return 0;
}
