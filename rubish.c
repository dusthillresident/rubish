#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
//#include <fcntl.h>
#include <poll.h>

#define MAIN_PRINT_RESULT 0

struct string; struct item; struct var; struct interp; struct func; struct array;
// if refCount is not NULL, that means it is a heap string and requires free() management
struct string { unsigned int *refCount; unsigned int length; char *s; };
enum fileportFlags { FPWRITE = 0b1, FPREAD = 0b10, FPREADWRITE = 0b11, FPMANAGED = 0b100 };
struct fileport { unsigned int refCount; FILE* fp; int flags; };
enum itemType { UNDEFINED, NOTHING, SYMBOL, NUMBER, STRING, FUNCTION, ARRAY, FILEPORT, ERROR,   LPAREN, RPAREN, LBRACKET, RBRACKET, STOP, REF };
struct item { int type; union { void *ptr; double number; struct string string; struct func *func; struct array *array; struct fileport *fileport; } data; };
enum memoryResourceType { RESOURCE = 0b1, ARRAYRESOURCE = 0b10 };
struct array { unsigned int refCount, chainRefCount, nDims, *dims; size_t size; struct item *itemArray; int arrayContainsMemoryResources; };
#define ERRORITEM(p)	((struct item){ERROR, {p}})
#define NOTHINGITEM	((struct item){NOTHING, {NULL}})
#define UNDEFINEDITEM 	((struct item){UNDEFINED, {NULL}})
struct item NUMBERITEM(double n){ struct item out; out.type=NUMBER; out.data.number=n; return out; }
struct var { int flags;  struct string name;  struct item *value;  struct var *prev; struct var *next;  unsigned int contextLevel; struct array *parentArray; };
#define ERROR_MESSAGE_BUFFER_SIZE 64
struct interp { struct var *vars; char *errorMessage; char errorMessageBuffer[ERROR_MESSAGE_BUFFER_SIZE]; unsigned int contextLevel; double rndSeed; struct item returnValue; };
enum varFlag { VARLOCAL = 0b1, VARREF = 0b10, VARREST = 0b100, VARMANAGED = 0b1000 };
struct func { unsigned int refCount; struct item (*primitive)(struct interp*,char**); struct var *params; char *body; };
#define isValue(type)  ( !(type) || ( (type)>NOTHING && (type)<ERROR ) )

struct item  eval( struct interp *interp, char *text );
struct item  eval_( struct interp *interp, char *text, int inParens );
struct item  eval_p( struct interp *interp, char **p, int inParens );
struct item  callFunc( struct interp *interp, struct func *func, char **p );
struct item  primitive_MakeArray( struct interp *interp, char **p );
long fileSize( FILE *fp );
int checkFileIsAppropriate( struct interp *interp, char **p, struct item *fileport, int accessMode );
struct string charPtrToNewString( char *charPtr, unsigned int length );
struct item  primitive_Source( struct interp *interp, char **p );

int specialChar( char c ){ switch (c) { case '(': case ')': case '[': case ']': case ';': case '"': return 1; default: return 0; } }
int symbolChar(  char c ){ return c && !isspace(c) && !specialChar(c); }
void printItem( FILE *port, struct item item );
int paramsRemain( char **p );
void deleteItem( struct item *item );
void deleteAllVars( struct interp *interp, struct var *var );
void printString( FILE *port, struct string *string );
struct item*  lookupItemPtr( struct interp *interp, char **p, struct item *symbol, int *infoReturn, struct array **parentArrayReturn );
struct item*  indexIntoArray( struct interp *interp, char **p, struct item *errorReturn, struct array *array, int *infoReturn, struct array **parentArrayReturn );

struct item  getItem( char **text ){       getitem_start:
 while( **text && isspace(**text) ) *text += 1;
 struct item  item = (struct item){0,{0}}; item.type = NOTHING;
 switch( **text ){
  case '#': while( **text && **text != '\n' ) *text += 1; goto getitem_start;
  case 0: item.type = NOTHING; break;
  case '@': *text += 1; item.type = REF; break;
  case '[': *text += 1; item.type = LBRACKET; break;
  case ']': *text += 1; item.type = RBRACKET; break;
  case '(': *text += 1; item.type = LPAREN; break;
  case ')': *text += 1; item.type = RPAREN; break;
  case ';': *text += 1; item.type = STOP; break;
  case '"': {
   *text += 1;
   unsigned int length = 0;  char *start = *text;
   while( **text && **text != '"' ){  *text += 1;  length++;  }
   *text += !!(**text);
   item.type = STRING;  item.data.string = (struct string){ NULL, length, start };
  } break;
  default: {
   unsigned int length = 0;  char *start = *text;
   while( symbolChar(**text) ){  *text += 1;  length++;  }
   if(  isdigit(*start) || (*start=='-' && isdigit(start[1]))  ){
    item.type = NUMBER;  char *dummy;  item.data.number = strtod( start, &dummy );
   }else{
    item.type = SYMBOL;  item.data.string = (struct string){ NULL, length, start };
   }
  }
 }
 return item;
}

struct item  peekItem( char **text ){
 char *ptr = *text;
 return getItem( &ptr );
}

void deleteString( struct string s ){
 if( s.refCount ){ 
  if( *s.refCount -= 1 ) return;
  free( s.refCount );  free(s.s);
 }
}

// the following functions implement the functionality of 'checkAndDeleteIfArrayIsOrphanedChain', which called by 'deleteItem' for arrays that contain arrays,
// rubish allows you to store arrays within arrays, and build linked chains of arrays, and in this scenario extra checking of references is necessary to ensure that memory resources aren't leaked

#define CH_DEFAULT_SIZE 48
struct CHitem { void *ptr; unsigned int count; int supported; int containsThis; };  struct checkHistory { size_t size, n; struct CHitem *ptrs; };

int ch_alreadyChecked( struct checkHistory *checked, void *ptr ){
 for(  size_t i = 0;  i < checked->n;  i++  ){  if( checked->ptrs[i].ptr == ptr ){  checked->ptrs[i].count += 1;  return 1;  }  }
 return 0;
}

unsigned int ch_getIndex( struct checkHistory *checked, void *ptr ){
 for(  size_t i = 0;  i < checked->n;  i++  ){  if( checked->ptrs[i].ptr == ptr ){ return i; }  }
 fprintf( stderr, "ch_getIndex: oh no\n");  exit(1);
}

unsigned int ch_addPtr( struct checkHistory *checked, void *ptr ){
 if( checked->n == checked->size ){
  checked->size += CH_DEFAULT_SIZE;  checked->ptrs = realloc( checked->ptrs, checked->size * sizeof(struct CHitem) );
 }
 unsigned int index = checked->n;  checked->ptrs[ index ].ptr = ptr;  checked->ptrs[ index ].count = 1;  checked->n += 1;
 return index;
}

int ch_surveyChain( struct checkHistory *checked, struct array *haystack, void *needle, int supported ){
 int arrayContainsMemoryResources_updated = 0;
 size_t count = 0;
 if( ! supported ) supported = ( haystack->refCount > haystack->chainRefCount );
 unsigned int index = ch_addPtr( checked, haystack );  checked->ptrs[index].containsThis = 0;
 for(  size_t i = 0;  i < haystack->size;  i++  ){
  struct item *item = haystack->itemArray + i;
  if( item->type == ARRAY && ( item->data.array->arrayContainsMemoryResources & ARRAYRESOURCE ) && item->data.array->refCount ){
   if( item->data.array == needle ){
    if( supported ) return 1;
    count += 1;  checked->ptrs[0].count += 1;  checked->ptrs[index].containsThis = 1;
   }else if( ! ch_alreadyChecked(checked,item->data.array) ){
    if( ch_surveyChain( checked, item->data.array, needle, supported ) ) return 1;
   }
  }
  // while we're at it, update info for this array
  switch( item->type ){
   case ARRAY:  arrayContainsMemoryResources_updated |= ARRAYRESOURCE;  break;
   case FUNCTION: case STRING: case FILEPORT:  arrayContainsMemoryResources_updated |= RESOURCE;  break;
  }
 }
 haystack->arrayContainsMemoryResources = arrayContainsMemoryResources_updated;
 return 0;
}

int ch_checkIfSupported( struct checkHistory *surveyed, struct array *array, struct array *this ){
 surveyed->ptrs[0].count -= 1;  int supported = 0;
 for(  unsigned int i = 0;  i < surveyed->n;  i++  ){
  supported += (surveyed->ptrs[i].supported = (surveyed->ptrs[i].count < ((struct array*)surveyed->ptrs[i].ptr)->refCount));
  if( surveyed->ptrs[i].supported && surveyed->ptrs[i].containsThis ) return 1;
 }
 if( ! supported ) return 0;
 propagate_supported_loop:
 for(  unsigned int i = 0;  i < surveyed->n;  i++  ){
  if( surveyed->ptrs[i].supported && surveyed->ptrs[i].count ){
   surveyed->ptrs[i].count = 0;  struct array *thisArray = (struct array*)surveyed->ptrs[i].ptr;
   for(  size_t j = 0;  j < thisArray->size;  j++  ){
    struct item *item = thisArray->itemArray + j;
    if( item->type == ARRAY && ( item->data.array->arrayContainsMemoryResources & ARRAYRESOURCE ) && item->data.array->refCount ){
     if( item->data.array == this ) return 1;
     unsigned int index = ch_getIndex( surveyed, item->data.array );
     if( ! surveyed->ptrs[index].supported ){  surveyed->ptrs[index].supported = 1;  if( index < i ) i = index-1;  }
    }
   }
  }
 }
 return 0;
}

void checkAndDeleteIfArrayIsOrphanedChain( struct item *item ){
 struct array *array = item->data.array;
 struct checkHistory checked;  checked.n = 0;  checked.size = CH_DEFAULT_SIZE;  checked.ptrs = malloc( sizeof(struct CHitem) * checked.size );
 int rootIsSupported = ch_surveyChain( &checked, array, array, 0 );
 if( rootIsSupported ){  free( checked.ptrs );  return;  }
 unsigned int count = checked.ptrs[0].count-1;
 if( count == array->refCount && ! ch_checkIfSupported( &checked, array, array ) ){
  array->refCount = 1;  deleteItem( item );
 }
 free( checked.ptrs );
}

void deleteItemWithParentArray( struct item *item, struct array *parentArray ){
 switch( item->type ){
  case ARRAY: {
    struct array *array = item->data.array;
    if( parentArray ){
     array->chainRefCount -= 1; 
    }
    if( ! array->refCount ) break;
    if( ! (array->refCount -= 1) ){
     if( array->arrayContainsMemoryResources ){
      size_t i; for( i=0; i<array->size; i++ ) deleteItemWithParentArray( array->itemArray + i, array );
     }
     free( array->dims );  free( array->itemArray );  free( array );
    }else if( array->refCount == array->chainRefCount && (array->arrayContainsMemoryResources & ARRAYRESOURCE) ){
     checkAndDeleteIfArrayIsOrphanedChain( item );
    }
   }
   break;
  case STRING:
   deleteString( item->data.string );
   break;
  case FUNCTION:
   if( ! (item->data.func->refCount -= 1) ){
    if( item->data.func->params ) deleteAllVars( NULL, item->data.func->params );
    free( item->data.func->body );
    free( item->data.func );
   }
   break;
  case FILEPORT:
   if( ! (item->data.fileport->refCount -= 1) ){
    if( ! ( item->data.fileport->flags & FPMANAGED ) ) fclose( item->data.fileport->fp );
    free( item->data.fileport );
   }
   break;
 }
}

void deleteItem( struct item *item ){
 deleteItemWithParentArray( item, NULL );
}

void deleteVar( struct interp *interp, struct var *var ){
 struct var *prev = var->prev;
 struct var *next = var->next;
 if( prev ) prev->next = next;
 if( next ) next->prev = prev;
 if( interp && interp->vars == var ){
  interp->vars = next; 
 }
 if( !(var->flags & VARREF) ){
  if( var->value ){  deleteItem( var->value );  free(var->value);  }
 }else{
  /* we don't need to worry about deleting the array here because refcount is guaranted to be greater than 1
     (an array must be stored in a variable to be referenced by a refvar) */
  if( var->parentArray ){  var->parentArray->refCount -= 1; }
 }
 deleteString(var->name);  free( var );
}

void deleteAllVars( struct interp *interp, struct var *var ){
 if( var->prev )
  var->prev->next = NULL; 
 while( var ){
  struct var *next = var->next;  deleteVar( interp, var );  var=next;
 }
}

int stringsMatch( struct string *a, struct string *b) {
 if(  a->length != b->length  ||  *a->s != *b->s  ||  a->s[a->length-1] != b->s[a->length-1]  ) return 0;
 unsigned int i; for( i=1; i < a->length-1; i++ ){ if( a->s[i] != b->s[i] ){ return 0; } }
 return 1;
}

struct var*  lookupVarByName( struct interp *interp, struct string *name ){
 struct var *vars = interp->vars;
 while( vars ){
  if( stringsMatch( &vars->name, name ) ) return vars;
  vars = vars->next;
 }
 return NULL;
}

struct item  lookupValueByName( struct interp *interp, struct string *name ){
 struct var *var = lookupVarByName( interp, name );
 if( var ) return *var->value;
 struct item out; out.type = NOTHING; return out;
}

struct string newCopyOfString( struct string source ){
 struct string out = source;
 out.refCount = malloc( sizeof(unsigned int) );  *out.refCount=1;  out.s = calloc( out.length+1, sizeof(char) );
 strncpy( out.s, source.s, source.length );
 return out;
}

void storeItem( struct item *dest, struct item *item, struct array *parentArray ){
 deleteItemWithParentArray( dest, parentArray );
 if( parentArray ){
  switch( item->type ){
   case ARRAY:  parentArray->arrayContainsMemoryResources |= ARRAYRESOURCE;  
    item->data.array->chainRefCount += 1;  break;
   case FUNCTION: case STRING: case FILEPORT:  parentArray->arrayContainsMemoryResources |= RESOURCE;  break;
  }
 }
 switch( item->type ){
  case ARRAY: item->data.array->refCount += 1; break;
  case FUNCTION: item->data.func->refCount += 1; break;
  case STRING:
   if( item->data.string.refCount )
    *item->data.string.refCount += 1;
   else
    {  item->data.string = charPtrToNewString( item->data.string.s, item->data.string.length );  *item->data.string.refCount += 1;  }
   break;
  case FILEPORT: item->data.fileport->refCount += 1; break;
 }
 *dest = *item;
}

struct var*  makeVar( struct item value,  struct string name  ){
 struct var *newVar = calloc( 1, sizeof(struct var) );
 newVar->value = calloc( 1, sizeof(struct item) );
 storeItem( newVar->value, &value, NULL );
 newVar->name = newCopyOfString( name );
 return newVar;
}

struct var* makeRefVar( struct interp *interp, struct item *target, struct string name, struct array *parentArray ){
 struct var *newVar = calloc( 1, sizeof(struct var) );  newVar->contextLevel = interp->contextLevel;
 newVar->value = target;
 newVar->flags = VARLOCAL | VARREF;
 newVar->name = name;  newVar->name.refCount = NULL;
 if( parentArray ){  newVar->parentArray = parentArray;  parentArray->refCount += 1;  }
 return newVar;
}

struct var* installVar( struct interp *interp, struct var *var ){
 var->next = interp->vars;  if(interp->vars) interp->vars->prev = var;
 interp->vars = var;
 return var;
}

struct item  getValue( struct interp *interp, char **p ){
 struct item  item = getItem( p );  int ref=0;
 getValue_start:
 switch( item.type ){
  case ARRAY: {
   if( peekItem(p).type == LBRACKET ){
    int infoReturn = 0;  struct item errorReturn; 
    struct item *itemPtr = indexIntoArray( interp, p, &errorReturn, item.data.array, &infoReturn, NULL );
    if( infoReturn ){  deleteItem( &item );  return errorReturn;  }
    struct item newItem;  newItem.type = 0;  storeItem( &newItem, itemPtr, NULL );  deleteItem( &item );
    if( newItem.type == ARRAY ){  item = newItem;  goto getValue_start;  }
    return newItem;
   }
   return item;
  }
  case RPAREN: case RBRACKET: case STOP:
   return NOTHINGITEM;
  case NOTHING: case NUMBER: case STRING: case ERROR: case UNDEFINED:
   return item;
  case LBRACKET: {
   interp->errorMessage = "syntax error or attempt to index something that isn't an array";  return ERRORITEM(*p);
  }
  case REF: {
   ref = 1;  item = getItem( p);  goto getValue_start;
  }
  case SYMBOL: {
   int infoReturn = 0;  struct item *itemPtr = lookupItemPtr( interp, p, &item, &infoReturn, NULL );  if( infoReturn ) return item;
   if( ! itemPtr ){  fprintf(stderr, "symbol is: ");  printString(stderr,&item.data.string);  putchar(10);  interp->errorMessage = "unknown symbol";  return ERRORITEM(*p);  }

   if( ref || itemPtr->type != FUNCTION ){
    storeItem( &item, itemPtr, NULL ); // increment ref count for the free-floating instance of the item
    if( item.type == ARRAY ) goto getValue_start;
    return item;
   }else{
    item = *itemPtr;
   }
   goto getValue_start;
  } break;
  case LPAREN: {
   item = getValue( interp, p );  if( item.type == ERROR ) return item;
   struct item rParen = getItem( p );
   if( rParen.type != RPAREN ){
    interp->errorMessage = rParen.type == STOP ? "expected ')' (note: you can't use ';' inside an expression)" : "expected ')'";
    deleteItem( &item );
    return ERRORITEM(*p);
   }
   if( ref || item.type == ARRAY ) goto getValue_start;
   return item;
  } break;
  case FUNCTION: {
   struct item returnValue = item.data.func->primitive ? item.data.func->primitive( interp, p ) : callFunc( interp, item.data.func, p );
   if( ref ) deleteItem( &item );
   if( returnValue.type == ARRAY ){  item = returnValue;  goto getValue_start;  }
   return returnValue;
  } break;
 }
 //fprintf(stderr,"getValue: FIXME: unhandled: ");  printItem( stderr, item );  fprintf(stderr,"\n");
 return item;
}

struct item* indexIntoArray( struct interp *interp, char **p, struct item *errorReturn, struct array *array, int *infoReturn, struct array **parentArrayReturn ){
 getItem(p);
 if( parentArrayReturn ) *parentArrayReturn = array;
 unsigned int i;  unsigned int arrayIndex = 0;
 for(  i = 0;  i < array->nDims;  i ++  ){
  struct item index = getValue(interp,p);  if( index.type == ERROR ){  *errorReturn = index;  *infoReturn = 1;  return NULL;  }
  if( index.type != NUMBER ){  deleteItem( &index );  interp->errorMessage = "bad array subscript";  *errorReturn = ERRORITEM(*p);  *infoReturn = 1;  return NULL;  }
  unsigned int thisIndex = (long long int)index.data.number;
  if(  thisIndex < 0  ||  thisIndex >= array->dims[i]  ){
   interp->errorMessage = "array subscript out of range";  *errorReturn = ERRORITEM(*p);  *infoReturn = 1;  return NULL; 
  }
  arrayIndex  =  arrayIndex * array->dims[i]  +  thisIndex;
 }
 if( getItem(p).type != RBRACKET ){  interp->errorMessage = "missing ']' or too many dimensions in array index";  *errorReturn = ERRORITEM(*p);  *infoReturn = 1;  return NULL;  }
 return array->itemArray + arrayIndex;
}

struct item*  lookupItemPtr( struct interp *interp, char **p, struct item *symbol, int *infoReturn, struct array **parentArrayReturn ){
 struct var *var = lookupVarByName( interp, &symbol->data.string );
 // Unbound symbol case
 if( ! var ){  return NULL;  }
 if(  (var->value->type == ARRAY) && (peekItem(p).type == LBRACKET)  ){
  // Array indexing case
  struct item *itemPtr = indexIntoArray( interp, p, symbol, var->value->data.array, infoReturn, parentArrayReturn );
  lookupItemPtr_array_indexing_loop:
  if( !*infoReturn && itemPtr->type == ARRAY && peekItem(p).type == LBRACKET ){
   itemPtr = indexIntoArray( interp, p, symbol, itemPtr->data.array, infoReturn, parentArrayReturn );
   if( !*infoReturn && itemPtr->type == ARRAY) goto lookupItemPtr_array_indexing_loop;
  }
  return itemPtr;
 }else{
  // Normal variable access case
  if( parentArrayReturn ) *parentArrayReturn = var->parentArray;
  return var->value;
 }
}

char *typeMismatchString[] = { "'undefined'", "'nothing'", "expected symbol", "expected number", "expected string", "expected function", "expected array", "expected fileport", "?1", "?2", "?3", "?4", "?5", "?6", "?7", "?8", "?9", "?a", "?b" };

struct item  getValueByType( struct interp *interp, char **p, int type ){
 char *start = *p;  struct item result = getValue(interp,p);
 if( result.type == ERROR ) return result;
 if( result.type != type ){
  if( type == NUMBER && result.type == UNDEFINED ){  result.type = NUMBER;  result.data.number = 0;  return result;  }
  interp->errorMessage = paramsRemain(&start) ? typeMismatchString[type] : "expected a value";
  deleteItem( &result );
  return ERRORITEM(*p);
 }
 return result;
}

struct item  getNumber( struct interp *interp, char **p ){
 return getValueByType( interp, p, NUMBER );
}

struct item  getString( struct interp *interp, char **p ){
 return getValueByType( interp, p, STRING );
}

struct item  getString_AllowUndefinedItem( struct interp *interp, char **p ){
 struct item item = getValue( interp, p );  if( item.type == ERROR ) return item;
 if( item.type == UNDEFINED ){  item.type = STRING;  item.data.string = (struct string){ NULL, 0, "" };  }
 if( item.type != STRING ){  deleteItem( &item );  interp->errorMessage = "expected string";  item = ERRORITEM(*p);  }
 return item;
}

struct string charPtrToString( char *charPtr ){
 struct string out;  out.refCount = NULL;  out.length = strlen( charPtr );  out.s = charPtr;  return out;
}

struct string charPtrToNewString( char *charPtr, unsigned int length ){
 struct string out;  
 out.refCount = malloc( sizeof(unsigned int) );  *out.refCount = 1;  out.length = length;
 out.s = malloc( 1+length );  out.s[length] = 0;  memcpy( out.s, charPtr, length );
 return out;
}

void installPrimitive(  struct interp *interp,  struct item (*primitive)(struct interp*,char**),  char *name ){
 struct func *func = calloc( 1, sizeof(struct func) );  func->primitive = primitive; func->refCount = 0;
 struct item item;  item.type = FUNCTION;  item.data.func = func;
 installVar( interp, makeVar( item, charPtrToString( name ) ) );
}

void printString( FILE *port, struct string *string ){
 unsigned int i;
 for( i=0; i<string->length; i++ ) fputc( string->s[i], port );
}

void printItem( FILE *port, struct item item ){
 switch( item.type ){
  case NOTHING:		fprintf( port, "[NOTHING]" ); break;
  case UNDEFINED:	fprintf( port, "[UNDEFINED]" ); break;
  case SYMBOL:		fprintf( port, "[SYMBOL:'" ); printString( port, &item.data.string); fprintf( port, "']" );  break;
  case NUMBER:		
   if( (long long int)item.data.number == item.data.number )
    fprintf( port, "%lld", (long long int)item.data.number );
   else
    fprintf( port, "%.16f", item.data.number );
   break;
  case STRING:		printString( port, &item.data.string ); break;
  case LPAREN:		fprintf( port, "[(]" ); break;
  case RPAREN:		fprintf( port, "[)]" ); break;
  case LBRACKET:	fprintf( port, "'['" ); break;
  case RBRACKET:	fprintf( port, "']'" ); break;
  case STOP:		fprintf( port, "[;]" ); break;
  case FUNCTION:	fprintf( port, "[FUNCTION]" ); break;
  case ARRAY:		fprintf( port, "[ARRAY]" ); break;
  case ERROR:		fprintf( port, "[ERROR]" ); break;
  case FILEPORT:	fprintf( port, "[FILEPORT]" ); break;
  default: fprintf( port, "[printItem default case]" );
 }
}

int paramsRemain( char **p ){
 struct item item = peekItem( p );
 return !(item.type == STOP || item.type == NOTHING || item.type == RPAREN || item.type == RBRACKET);
}

int skipParen( char **p ){
 int level = 1;
 if( getItem(p).type != LPAREN ){ fprintf(stderr, "oh no, skipParen\n"); return 0; }
 while(1){
  struct item item = getItem(p);
  switch( item.type ){
   case LPAREN: level += 1; break;
   case RPAREN: level -= 1; break;
   case NOTHING: return level;
  }
  if(!level){
   return level;
  }
 }
 return level;
}

struct array* makeSimpleArray( struct item *value ){
 struct array *array = calloc( 1, sizeof(struct array) );
 array->refCount = 1;  array->nDims = 1;  array->dims = calloc( 1, sizeof(unsigned int) );  array->dims[0] = 1;  array->size = 1;
 array->itemArray = calloc( 1, sizeof(struct item) );
 storeItem( array->itemArray, value, array );
 deleteItem( value );
 return array;
}

void appendItemToSimpleArray( struct array *array, struct item *value ){
 array->dims[0] += 1;  array->size += 1;  array->itemArray = realloc( array->itemArray, sizeof(struct item)*array->size );
 (array->itemArray + (array->size - 1))->type = NOTHING;
 storeItem( array->itemArray + (array->size - 1), value, array );
 deleteItem( value );
}

#define ADDVAR(LIST,CURR,NEWVAR) {struct var *_NEW = NEWVAR; if( !LIST ) { LIST = _NEW;  CURR = _NEW; } else { CURR->next = _NEW;  CURR = _NEW; }}

struct item primitive_Func( struct interp *interp, char **p ){
 // examples:
 // set myfunction  func (a b) (+ a b) ;
 // func myfunction (a b) (+ a b) ;
 // @(func(a b)(+ a b)) 1 2 ;
 char *body = NULL; struct func *func = NULL;  struct var *params = NULL;  struct var *curParam = NULL;
 struct item result;  result.type = UNDEFINED;
 // ----
 int functionIsNamed = (peekItem(p).type == SYMBOL);  struct item functionNameSymbol;  if( functionIsNamed ) functionNameSymbol = getItem(p);
 if( getItem(p).type != LPAREN ){  interp->errorMessage = "func: syntax error: expected params list in parens";  return ERRORITEM(*p);  }
 // process params
 int varIsRestArray = 0;
 while( paramsRemain(p) ){
  // if '@' is found, this is a 'reference var'
  int varIsRef = (peekItem(p).type == REF);  if( varIsRef ) getItem(p);
  // expecting a symbol (name of this parameter variable)
  struct item paramName = getItem(p);  if( paramName.type != SYMBOL ){  interp->errorMessage = "func: syntax error: bad param list";  goto primitive_func_failure;  }
  // if '.' is found, the next thing expected is a symbol (the name of the 'rest array' variable)
  if( ! varIsRef && paramName.data.string.length == 1 && *paramName.data.string.s == '.' ){
   varIsRestArray = 1;  paramName = getItem(p);  if( paramName.type != SYMBOL ){  interp->errorMessage = "func: syntax error: bad param list";  goto primitive_func_failure;  }
  }
  // let's not allow duplicate parameter names 
  struct var *varP = params;
  while( varP ){
   if( stringsMatch( &paramName.data.string, &varP->name ) ){
    interp->errorMessage = "func: syntax error: duplicate parameter name";  goto primitive_func_failure;
   }
   varP = varP->next;
  }
  // create the entry for this parameter variable in the list of parameters for this function
  struct var *thisParam = calloc( 1, sizeof(struct var) );
  thisParam->name = newCopyOfString( paramName.data.string );  thisParam->flags = VARLOCAL | (varIsRef * VARREF) | (varIsRestArray * VARREST);
  ADDVAR( params, curParam, thisParam );
  // if this var is the 'rest array', we expect that to be the end of the list of parameters for this function
  if( varIsRestArray && peekItem(p).type != RPAREN ){
   interp->errorMessage = "func: syntax error: bad param list: the 'rest' variable must be the very last one";  goto primitive_func_failure;
  }
  //printf("   curParam->flags  == %d\n",  curParam->flags);
 }
 // expecting ')'
 if( getItem(p).type != RPAREN ){  interp->errorMessage = "func: syntax error: bad param list";  goto primitive_func_failure;  }
 // expecting function body contained in parens
 if( getItem(p).type != LPAREN ){
  interp->errorMessage = "func: syntax error: expected function body in parens";  goto primitive_func_failure; 
 }
 *p -= 1;  char *code_p = *p;
 if( skipParen(p) ){
  interp->errorMessage = "func: function body ')' missing";  goto primitive_func_failure;
 }
 char *end_p = *p;
 // create func struct & fill in the data
 func = calloc( 1, sizeof( struct func ) );  func->refCount = 1;  func->params = params; 
 body = calloc( (end_p-code_p), sizeof(char) );  strncpy( body, code_p+1, end_p-code_p-2 );
 func->body = body;
//printf("this: '%s'\n", body );
 result.type = FUNCTION;  result.data.func = func;
 // if named, find the variable, store the item
 if( functionIsNamed ){
  struct var *var = lookupVarByName( interp, &functionNameSymbol.data.string );
  if( ! var ) 
   installVar( interp, makeVar( result, functionNameSymbol.data.string ) );
  else 
   storeItem( var->value, &result, NULL );
 }
 // return here
 return result;
 primitive_func_failure:
 // goto this label if it failed somewhere, here we tidy up and return the error etc
 if(params) deleteAllVars( NULL, params );
 free( func );  free( body );
 return ERRORITEM(*p);
}

struct var*  makeLocalVar( struct interp *interp, struct item value,  struct string name  ){
 struct var *newVar = calloc( 1, sizeof(struct var) );  newVar->flags = VARLOCAL;  newVar->contextLevel = interp->contextLevel;
 newVar->value = calloc( 1, sizeof(struct item) );
 // Store the value without incrementing refCount. The 'free floating instance' is converted immediately into the 'stored instance' inside this local var.
 *newVar->value = value;
 newVar->name = name; newVar->name.refCount = NULL;
 return newVar;
}

struct item  primitive_Local( struct interp *interp, char **p ){
 if( ! interp->contextLevel ){  interp->errorMessage = "local: can't make local variables at top level";  return ERRORITEM(*p);  }
 struct var *locals = NULL;  struct var *lastLocalCreated = NULL;  struct item failureResult;
 while( paramsRemain(p) ){
  struct item varName = getItem(p);
  if( varName.type != SYMBOL ){  interp->errorMessage = "local: syntax error: expected symbol";  failureResult = ERRORITEM(*p);  goto primitive_local_failure;  }
  struct item value = UNDEFINEDITEM;
  if( peekItem(p).type == LPAREN ){
   value = getValue(interp,p);  if( value.type == ERROR ){  failureResult = value;  goto primitive_local_failure;  }
  }
  ADDVAR( locals, lastLocalCreated, makeLocalVar( interp, value, varName.data.string ) );
 }
 if( locals ){
  lastLocalCreated->next = interp->vars;  if( interp->vars ) interp->vars->prev = lastLocalCreated;
  interp->vars = locals;
 }
 return UNDEFINEDITEM;
 primitive_local_failure:
 if( locals ) deleteAllVars( NULL, locals );
 return failureResult;
}

const char *returnExceptionMsg = "! return exception";
struct item  primitive_Return( struct interp *interp, char **p ){
 struct item value = getValue(interp,p);  if( value.type == ERROR ) return value;
 interp->errorMessage = (char*) returnExceptionMsg;  interp->returnValue = value;  return ERRORITEM(*p);
}

#define VAR_IS_REF(VAR) (VAR->flags & VARREF)
#define VAR_IS_REST(VAR) (VAR->flags & VARREST)

struct item  callFunc( struct interp *interp, struct func *func, char **p ){
 struct var *previousTopVar = interp->vars;
 interp->contextLevel += 1;  unsigned int thisContextLevel = interp->contextLevel;
 struct item result = UNDEFINEDITEM;
 struct var *createdVars = NULL;  struct var *lastCreated = NULL;  struct var *createdNewVars = NULL; struct var *lastCreatedNewVar = NULL;
 struct var *curParam = func->params;
 // create local vars for the arguments, get the values and bind them
 while( curParam ){
  struct var *thisVar = NULL;
  if( VAR_IS_REF( curParam ) ){
   struct item symbol = getItem(p);
   if( symbol.type != SYMBOL ){  interp->errorMessage = "function call: expected name for variable passed by reference";  goto callfunc_failure;  }
   // ----
   int infoReturn = 0;  struct array *refVarParentArray = NULL;
   struct item *refItem = lookupItemPtr( interp, p, &symbol, &infoReturn, &refVarParentArray );
   if( infoReturn ){  result = symbol;  goto callfunc_failure;  }
   if( !refItem ){  
    // create var that didn't exist
    struct var *newVar = makeVar( UNDEFINEDITEM, symbol.data.string );  refItem = newVar->value;
    ADDVAR( createdNewVars, lastCreatedNewVar, newVar );
   }
   thisVar = makeRefVar( interp, refItem, curParam->name, refVarParentArray );
  }else if( VAR_IS_REST( curParam ) ){
   struct item restArray = primitive_MakeArray(interp,p);  if( restArray.type == ERROR ){  result = restArray;  goto callfunc_failure;  }
   thisVar = makeLocalVar( interp, restArray, curParam->name ); 
  }else{
   struct item value = getValue(interp,p);  if( value.type == ERROR ){  result = value;  goto callfunc_failure;  }
   if( value.type == NOTHING ) {  interp->errorMessage = "function call: not enough arguments to function";  goto callfunc_failure;  }
   thisVar = makeLocalVar( interp, value, curParam->name );
  }
  ADDVAR( createdVars, lastCreated, thisVar);
  curParam = curParam->next;
 }
 // install the created locals 
 if( createdVars ){
  lastCreated->next = interp->vars;  if( interp->vars ) interp->vars->prev = lastCreated;
  interp->vars = createdVars;
 }
 // evaluate the function body
 result = eval( interp, func->body );  interp->contextLevel -= 1;
 // save the result, cleanup the local vars and stuff
 struct var *varP = interp->vars;
 while(1){
  struct var *nextVar = varP->next;
  if( varP->contextLevel == thisContextLevel ) deleteVar( interp, varP );
  varP = nextVar;  if( ! varP || varP==previousTopVar ) break;  
 }
 if( createdNewVars ){
  lastCreatedNewVar->next = interp->vars;  interp->vars = createdNewVars;
 }
 // Catch return exception
 if( interp->errorMessage == returnExceptionMsg ){  result = interp->returnValue;  interp->returnValue.type = 0;  interp->errorMessage = "retexp";  }
 return result; 
 callfunc_failure:
 if( result.type != ERROR ){  deleteItem( &result );  result = ERRORITEM(*p);  }
 // cleanup and return error
 while( createdVars ){
  struct var *nextVar = createdVars->next;  deleteVar( NULL, createdVars );  createdVars = nextVar;
 }
 while( createdNewVars ){
  struct var *nextVar = createdNewVars->next;  deleteVar( NULL, createdNewVars );  createdNewVars = nextVar;
 }
 return result;
}

void installManagedFileport( struct interp *interp, FILE *fp, int accessMode, char *name ){
 struct fileport *fileport = calloc( 1, sizeof(struct fileport) );  fileport->refCount = 0;  fileport->fp = fp;  fileport->flags = accessMode | FPMANAGED;
 struct item item;  item.type = FILEPORT;  item.data.fileport = fileport;
 installVar( interp, makeVar( item, charPtrToString( name ) ) );
}

struct item getNullTerminatedString( struct interp *interp, char **p ){
 struct item string = getString(interp,p);  if( string.type == ERROR) return string;
 if( ! string.data.string.refCount ){
  struct string s = newCopyOfString( string.data.string );  deleteItem( &string );  string.data.string = s;
 }
 return string;
}

struct item  primitive_Openfile( struct interp *interp, char **p, int accessMode ){
 struct item filepath = getNullTerminatedString(interp,p);  if( filepath.type == ERROR) return filepath;
 char *mode = NULL;
 switch( accessMode ){
  case FPREAD:      mode = "rb"; break;
  case FPWRITE:     mode = "wb"; break;
  case FPREADWRITE: mode = "rb+"; break;
  default: fprintf( stderr, "oh no: primitive_Openfile\n" );
 }
 FILE *fp = fopen( filepath.data.string.s, mode );
 struct item result = UNDEFINEDITEM;
 if( ! fp ){
  switch( errno ){
   case EINVAL: fprintf( stderr, "oh no: EINVAL\n" ); break;
   case ENOENT: interp->errorMessage =		"0  : file doesn't exist"; break;
   case EACCES: interp->errorMessage =		"1  : access denied"; break;
   case ENAMETOOLONG: interp->errorMessage =	"2  : path was too long"; break;
   case ENOSPC: interp->errorMessage =		"3  : no space on filesystem to create new file"; break;
   case ENOTDIR: interp->errorMessage =		"4  : a component used as a directory in the path was actually not a directory"; break;
   case EMFILE: interp->errorMessage =		"5  : too many open files"; break;
   case ENFILE: interp->errorMessage =		"6  : the system-wide limit on the total number of open files has been reached"; break;
   case EISDIR: interp->errorMessage =		"7  : path refers to a directory and the access requested involved writing"; break;
   case ELOOP:  interp->errorMessage =		"8  : too many symbolic links were encountered in resolving the path"; break;
   case ENOMEM: interp->errorMessage =		"9  : run out of memory"; break;
   default: interp->errorMessage =		"10 : couldn't open file"; break;
  }
 }else{
  struct fileport *fileport = calloc( 1, sizeof(struct fileport) );  fileport->refCount = 1;  fileport->fp = fp;  fileport->flags = accessMode;
  result.type = FILEPORT;  result.data.fileport = fileport;
 }
 deleteItem( &filepath );
 return result;
}

struct item  primitive_Openin ( struct interp *interp, char **p ){
 return primitive_Openfile( interp, p, FPREAD );
}
struct item  primitive_Openout( struct interp *interp, char **p ){
 return primitive_Openfile( interp, p, FPWRITE );
}
struct item  primitive_Openup ( struct interp *interp, char **p ){
 return primitive_Openfile( interp, p, FPREADWRITE );
}

#define FILE_PRIMITIVE( NAME, FILE_FUNCTION, ACCESS_MODE ) \
struct item  primitive_##NAME( struct interp *interp, char **p ){ \
 struct item fileportItem = getValue(interp,p);  if( ! checkFileIsAppropriate( interp, p, &fileportItem, ACCESS_MODE ) ) return fileportItem; \
 struct item result;  result.type = NUMBER;  result.data.number = FILE_FUNCTION( fileportItem.data.fileport->fp ); \
 deleteItem( &fileportItem );  return result; \
}
FILE_PRIMITIVE( FileSize, fileSize, 0 )
FILE_PRIMITIVE( FileEof, feof, 0 )
FILE_PRIMITIVE( FileTell, ftell, 0 )
FILE_PRIMITIVE( FileGetByte, fgetc, FPREAD )
FILE_PRIMITIVE( FileFlush, fflush, FPWRITE )

struct string  heapCharPtrToString( char *charPtr, unsigned int length ){
 struct string out;  out.refCount = malloc( sizeof(unsigned int) );  *out.refCount = 1;  out.length = length;  out.s = charPtr;
 return out;
}

struct item  primitive_FileGetLine( struct interp *interp, char **p ){
 struct item fileportItem = getValue(interp,p);  if( ! checkFileIsAppropriate( interp, p, &fileportItem, FPREAD ) ) return fileportItem;
 FILE *fp = fileportItem.data.fileport->fp;  char *buf = malloc(128);  unsigned int bufSize = 128, length = 0;
 while(1){
  int c = fgetc( fp );
  if( c == '\r' ){  c = fgetc( fp );  if( c != -1 && c != '\n' ){  ungetc( c, fp );  }  break;  }
  if( c == '\n' || c == -1 ) break;
  buf[ length ] = c;  length ++;  if( bufSize - length < 2 ){  bufSize += 64;  buf = realloc( buf, bufSize );  }
 }
 buf[ length ] = 0;
 struct item out;  out.type = STRING;  out.data.string = heapCharPtrToString( buf, length );
 deleteItem( &fileportItem );  return out;
}

struct item  primitive_FileGetChar( struct interp *interp, char **p ){
 struct item fileportItem = getValue(interp,p);  if( ! checkFileIsAppropriate( interp, p, &fileportItem, FPREAD ) ) return fileportItem;
 int v = fgetc( fileportItem.data.fileport->fp );  char s[] = { v, 0 };
 struct item out;  out.type = STRING;  out.data.string = charPtrToNewString( s, 1);  if( v == -1 ) out.data.string.length = 0;
 deleteItem( &fileportItem );  return out;
}

struct item  primitive_FilePutChar( struct interp *interp, char **p ){
 struct item fileportItem = getValue(interp,p);  if( ! checkFileIsAppropriate( interp, p, &fileportItem, FPWRITE ) ) return fileportItem;
 struct item charValue = getValue(interp,p);  unsigned char charValueV = 0;
 switch( charValue.type ){
  case ERROR:  deleteItem( &fileportItem ); return charValue;
  case STRING:
   if( charValue.data.string.length ) charValueV = *(unsigned char*)charValue.data.string.s;
   deleteItem( &charValue );
   break;
  case NUMBER: charValueV = charValue.data.number; break;
  default: deleteItem( &fileportItem );  deleteItem( &charValue );  interp->errorMessage = "file-put-char: wrong type";  return ERRORITEM(*p);
 }
 struct item result;  result.type = NUMBER;  result.data.number = fputc( charValueV, fileportItem.data.fileport->fp );
 deleteItem( &fileportItem );  return result; 
}

struct item primitive_FileRead( struct interp *interp, char **p ){
 struct item result;  result.type = STRING;
 // get the fileport to read from
 struct item fileportItem = getValue(interp,p);  if( ! checkFileIsAppropriate( interp, p, &fileportItem, FPREAD ) ) return fileportItem;
 FILE *fp = fileportItem.data.fileport->fp;
 size_t bytesRemainingInFile = fileSize( fp ) - ftell( fp );  size_t bytes = bytesRemainingInFile;
 // get the optional 'number of bytes to read' parameter, if it is provided
 if( paramsRemain(p) ){
  struct item bytesItem = getNumber(interp,p);  if( bytesItem.type == ERROR ){  deleteItem( &fileportItem );  return bytesItem;  }
  if( bytesItem.data.number <= 0 ){  result.data.string = (struct string){ NULL, 0, "" };  goto primitive_FileRead_exit;  }
  bytes = (bytesItem.data.number > bytesRemainingInFile) ? bytesRemainingInFile : bytesItem.data.number; 
 }
 // create the string and read the data
 if( (size_t)(unsigned int) bytes != bytes ){
  interp->errorMessage = "file-read: unable to create a string this big";  result = ERRORITEM(*p);
 }else{
  char *str = malloc( bytes + 1 );  bytes = fread( (void*)str, 1, bytes, fp );  str[bytes] = 0;
  result.data.string = heapCharPtrToString( str, bytes );
 }
 // return
 primitive_FileRead_exit:
 deleteItem( &fileportItem );  return result;
}

struct item primitive_InputReady( struct interp *interp, char **p ){
 struct item fileportItem = getValue(interp,p);  if( ! checkFileIsAppropriate( interp, p, &fileportItem, FPREAD ) ) return fileportItem;
 struct item result;
 FILE *fp = fileportItem.data.fileport->fp;  int fd = fileno(fp);
 if( fd == -1 ){
  interp->errorMessage = "input-ready?: unable to get file descriptor";  result = ERRORITEM(*p);
 }else{
  result.type = NUMBER;  struct pollfd fds;  fds.fd = fd;  fds.events = POLLIN;  int r = poll( &fds, 1, 0 );
  result.data.number = (r > 0 && (fds.revents & POLLIN));  deleteItem( &fileportItem );
 }
 return result;
}

struct item primitive_FileWrite( struct interp *interp, char **p ){
 // get the fileport to read from
 struct item fileportItem = getValue(interp,p);  if( ! checkFileIsAppropriate( interp, p, &fileportItem, FPWRITE ) ) return fileportItem;
 FILE *fp = fileportItem.data.fileport->fp;
 // get the string to write
 struct item string = getString( interp, p );  if( string.type == ERROR ){  deleteItem( &fileportItem );  return string;  }
 // write string
 struct item result;  result.type = NUMBER;
 result.data.number = fwrite( string.data.string.s, 1, string.data.string.length, fp );
 // return
 deleteItem( &string );  deleteItem( &fileportItem );  return result;
}

struct item  primitive_FileSeek_( struct interp *interp, char **p, int whence ){
 struct item fileportItem = getValue(interp,p);  if( ! checkFileIsAppropriate( interp, p, &fileportItem, 0 ) ) return fileportItem;
 struct item index = getNumber(interp,p);  if( index.type == ERROR ){  deleteItem( &fileportItem );  return index;  }
 long offset = index.data.number;
 index.data.number = fseek( fileportItem.data.fileport->fp, offset, whence );  deleteItem( &fileportItem );  return index;
}

struct item  primitive_FileSeekTo( struct interp *interp, char **p ){
 return primitive_FileSeek_( interp, p, SEEK_SET );
}

struct item  primitive_FileSeekFrom( struct interp *interp, char **p ){
 return primitive_FileSeek_( interp, p, SEEK_CUR );
}

struct item  primitive_FileSeekFromEnd( struct interp *interp, char **p ){
 return primitive_FileSeek_( interp, p, SEEK_END );
}

int checkFileIsAppropriate( struct interp *interp, char **p, struct item *fileport, int accessMode ){
 if( fileport->type == ERROR ) return 0;
 if( fileport->type != FILEPORT ){  interp->errorMessage = "expected a fileport";  goto checkFileIsAppropriate_failure;  }
 if( accessMode && ((fileport->data.fileport->flags & accessMode) != accessMode) ){
  switch(accessMode){
   case FPREAD: interp->errorMessage = "expected a readable fileport"; break;
   case FPWRITE: interp->errorMessage = "expected a writable fileport"; break;
   case FPREADWRITE: interp->errorMessage = "expected a fileport which is both readable and writable"; break;
   default: interp->errorMessage = "oh no, bad accessMode, this shouldn't happen"; break;
  }
  goto checkFileIsAppropriate_failure;
 }
 return 1;
 checkFileIsAppropriate_failure:
 deleteItem( fileport );  *fileport = ERRORITEM(*p); return 0;
}

struct item  primitive_Print( struct interp *interp, char **p ){
 FILE *port = stdout;  struct item portItem = UNDEFINEDITEM;
 struct item item;   int printTab = 0;
 if( paramsRemain(p) ){
  // If the first argument is a fileport, we will print to that fileport
  item = getValue( interp, p);
  if( item.type == FILEPORT ){
   if( ! checkFileIsAppropriate( interp, p, &item, FPWRITE ) ) return item;
   port = item.data.fileport->fp;  portItem = item;
  }else{
   goto primitive_Print_jump_in;
  }
  // print all arguments
  while( paramsRemain(p) ){
   if( printTab) fprintf( port, "	" );
   item = getValue( interp, p );
   primitive_Print_jump_in:
   if( item.type == ERROR ) return item;
   printItem( port, item );  deleteItem( &item );
   printTab = 1;
  }
 }
 if( peekItem(p).type == STOP ) fprintf( port, "\n" );
 deleteItem( &portItem );
 struct item out;  out.type = UNDEFINED;  return out;
}

struct item  primitive_Set( struct interp *interp, char **p ){
 // ensure we got a symbol
 struct item varName = getItem( p );
 if( varName.type != SYMBOL ){
  interp->errorMessage = "'set': syntax error: expected a symbol";
  return ERRORITEM(*p);
 }
 // look up the variable/array item specified
 int infoReturn = 0;  struct array *parentArray = NULL;  struct item *itemPtr = lookupItemPtr( interp, p, &varName, &infoReturn, &parentArray );
 if( infoReturn ) return varName; // this handles case of array indexing failure
 // get the value to be stored
 struct item value = getValue( interp, p );  if( value.type == ERROR ) return value; 
 if( !isValue(value.type) ){  interp->errorMessage = "set: must be given a value";  return ERRORITEM(*p);  }
 // if variable didn't exist, we create it now
 if( ! itemPtr ) {
  struct var *var = makeVar( UNDEFINEDITEM, varName.data.string );  installVar( interp, var);  itemPtr = var->value;
 }
 // store the value
 storeItem( itemPtr, &value, parentArray );
 return value;
}

#define MATHS_PRIMITIVE( NAME, OPERATION ) \
struct item  primitive_##NAME( struct interp *interp, char **p ){ \
 struct item  a = getNumber( interp,p ); if( a.type == ERROR ) return a; \
 struct item  b = getNumber( interp,p ); if( b.type == ERROR ) return b; \
 a.data.number OPERATION##= b.data.number; \
 while( paramsRemain(p) ){ \
  b = getNumber( interp,p ); if( b.type == ERROR ) return b; \
  a.data.number OPERATION##= b.data.number; \
 } \
 return a; \
}
MATHS_PRIMITIVE(Add,+)
MATHS_PRIMITIVE(Sub,-)
MATHS_PRIMITIVE(Mul,*)
MATHS_PRIMITIVE(Div,/)

struct item  primitive_Modulo( struct interp *interp, char **p ){
 struct item  a = getNumber( interp,p ); if( a.type == ERROR ) return a;
 struct item  b = getNumber( interp,p ); if( b.type == ERROR ) return b;
 int aa = a.data.number, bb = b.data.number;
 if( ! bb ){  interp->errorMessage = "modulo division by zero";  return ERRORITEM(*p);  }
 a.data.number = aa % bb;
 return a;
}

#define SHIFT_PRIMITIVE( NAME, CAST, OPERATION ) \
struct item  primitive_##NAME( struct interp *interp, char **p ){ \
 struct item  a = getNumber( interp,p ); if( a.type == ERROR ) return a; \
 struct item  b = getNumber( interp,p ); if( b.type == ERROR ) return b; \
 a.data.number = (double)( ( CAST )a.data.number OPERATION ( CAST )b.data.number ); \
 return a; \
}
SHIFT_PRIMITIVE( ShiftLeft,  int, << )
SHIFT_PRIMITIVE( ShiftRight, int, >> )
SHIFT_PRIMITIVE( ShiftShiftRight, unsigned int, >> )

#define BITWISE_PRIMITIVE( NAME, OPERATION ) \
struct item  primitive_##NAME( struct interp *interp, char **p ){ \
 struct item  a = getNumber( interp,p ); if( a.type == ERROR ) return a; \
 struct item  b = getNumber( interp,p ); if( b.type == ERROR ) return b; \
 int result = (int)a.data.number OPERATION (int)b.data.number; \
 while( paramsRemain(p) ){ \
  a = getNumber( interp,p ); if( a.type == ERROR ) return a; \
  result = result OPERATION (int)a.data.number; \
 } \
 a.data.number = result; return a; \
}
BITWISE_PRIMITIVE( BitwiseAnd, & )
BITWISE_PRIMITIVE( BitwiseOr, | )
BITWISE_PRIMITIVE( BitwiseXor, ^ )

#define COMPARISON_PRIMITIVE( NAME, OPERATION ) \
struct item  primitive_##NAME( struct interp *interp, char **p ){ \
 struct item  a = getNumber( interp,p ); if( a.type == ERROR ) return a; \
 struct item  b = getNumber( interp,p ); if( b.type == ERROR ) return b; \
 a.data.number = a.data.number OPERATION b.data.number; \
 return a; \
}
COMPARISON_PRIMITIVE( Equal, == )
COMPARISON_PRIMITIVE( NotEqual, != )
COMPARISON_PRIMITIVE( Less, < )
COMPARISON_PRIMITIVE( LessEqual, <= )
COMPARISON_PRIMITIVE( Greater, > )
COMPARISON_PRIMITIVE( GreaterEqual, >= )

#define UNARY_MATHS_PRIMITIVE( NAME, TYPE, OPERATION ) \
struct item  primitive_##NAME( struct interp *interp, char **p ){ \
 struct item  a = getNumber( interp,p ); if( a.type == ERROR ) return a; \
 TYPE result = 0; \
 result = OPERATION ( TYPE ) a.data.number; \
 a.data.number = result; \
 return a; \
}
UNARY_MATHS_PRIMITIVE( Neg, double, - )
UNARY_MATHS_PRIMITIVE( Not, double, ! )
UNARY_MATHS_PRIMITIVE( BitwiseNot, int, ~ )

struct item  primitive_Int( struct interp *interp, char **p ){
 struct item  a = getNumber( interp,p ); if( a.type == ERROR ) return a;
 a.data.number = (int)a.data.number;
 return a;
}

struct item  primitive_Sgn( struct interp *interp, char **p ){
 struct item  a = getNumber( interp,p ); if( a.type == ERROR ) return a;
 a.data.number = a.data.number ? ( a.data.number < 0 ? -1.0 : 1.0 ) : 0.0;
 return a;
}

struct item  primitive_Abs( struct interp *interp, char **p ){
 struct item  a = getNumber( interp,p ); if( a.type == ERROR ) return a;
 a.data.number = a.data.number < 0 ? - a.data.number : a.data.number;
 return a;
}

void  itemToBoolean( struct interp *interp, struct item *test, char **p ){
 switch( test->type ){
  case ERROR:  return;
  // For type 'NOTHING', this is an error, because we expected a value
  case NOTHING:  interp->errorMessage = "expected a value";  *test = ERRORITEM(*p);  return;
  // For type 'STRING', we consider the empty string to be 'false' and non empty strings to be 'true'
  case STRING:  deleteItem( test );  test->data.number = !!test->data.string.length;  break;
  // For type 'UNDEFINED', we consider this to be 'false', and all other types are considered 'true'
  // This behaviour is useful because functions that return arrays will return UNDEFINED in the case where the returned array would be empty.
  case UNDEFINED:  test->data.number = 0;  break;
  default:  deleteItem( test );  test->data.number = 1;  break;
 }
 test->type = NUMBER;
}

struct item  getBoolean( struct interp *interp, char **p ){
 struct item item = getValue(interp,p);
 if( item.type != NUMBER ) itemToBoolean( interp, &item, p );
 return item;
}

struct item  primitive_If( struct interp *interp, char **p ){
 // if [condition] ([main block]) [(else block)]
 // get the test condition
 struct item  test = getBoolean(interp,p);  if( test.type == ERROR ) return test;
 char *paren_p = *p;
 // we expect a code block contained in parens
 if( getItem(p).type != LPAREN ){
  interp->errorMessage = "if: syntax error: expected a code block contained in parens";
  return ERRORITEM(*p);
 }
 struct item result;  result.type = UNDEFINED;
 // if the test condition is true, we evaluate the main code block
 if( test.data.number ){
  result = eval_p(interp, p, 1);  if( result.type == ERROR ) return result;
  if( peekItem(p).type == LPAREN && skipParen(p) ){
   deleteItem( &result );  interp->errorMessage = "missing ')'";  return ERRORITEM(*p);
  }
 }else{
  // else, we skip the main code block, and if the 'else' code block is found, we evaluate that
  *p = paren_p;
  if( skipParen(p) ){
   deleteItem( &result );  interp->errorMessage = "missing ')'";  return ERRORITEM(*p);
  }
  char *saveP = *p;
  if( getItem(p).type == LPAREN ){
   result = eval_p(interp,p,1);
  }else *p = saveP;
 }
 // the result will be [UNDEFINED] if the main block was not evaluated, and there was no 'else' block
 return result;
}

int skipItems( char **p ){
 char *last;
 int plevel = 0, blevel = 0;
 while(1){
  last = *p;  struct item item = getItem(p);
  switch( item.type ){ 
   case LBRACKET:  blevel += 1;  break;
   case RBRACKET:  blevel -= 1;  break;
   case LPAREN:    plevel += 1;  break;
   case RPAREN:    plevel -= 1;  break;
   case NOTHING: return plevel || blevel;
  }
  if( plevel < 0 || blevel < 0 ){  *p = last;  return plevel > 0 || blevel > 0;  }
 }
}

#define LOGIC_PRIMITIVE( NAME, OPERATION, SKIPWHEN ) \
struct item  primitive_##NAME( struct interp *interp, char **p ){ \
 struct item  a = getBoolean( interp,p );  if( a.type == ERROR ) return a; \
 int result = !!a.data.number; \
 while( paramsRemain(p) ){ \
  if( result == SKIPWHEN ){ \
   if( skipItems(p) ){  interp->errorMessage = #OPERATION ": encountered mismatched parens or brackets when skipping";  return ERRORITEM(*p);  }\
   break; \
  } \
  a = getBoolean( interp,p );  if( a.type == ERROR ) return a; \
  result = result OPERATION !!a.data.number; \
 } \
 a.data.number = result; return a; \
}
LOGIC_PRIMITIVE( And, &&, 0 )
LOGIC_PRIMITIVE( Or, ||, 1 )

struct item  primitive_Same( struct interp *interp, char **p ){
 struct item  a = getValue(interp,p);  if( a.type == ERROR ) return a;
 if( a.type == NOTHING ){  interp->errorMessage = "same?: expected two values to compare";  return ERRORITEM(*p);  }
 struct item  b = getValue(interp,p);  if( b.type == ERROR ){  deleteItem( &a );  return b;  }
 struct item  result;  result.type = NUMBER;  result.data.number = 0;
 if( a.type == b.type ){
  switch( a.type ){
   case NUMBER:  case UNDEFINED:  case NOTHING:  
    return result;
   case ARRAY:  case FUNCTION:  case FILEPORT:
    result.data.number = ( a.data.ptr == b.data.ptr );  break;
   case STRING:
    result.data.number = ( a.data.string.s == b.data.string.s && a.data.string.length == b.data.string.length );  break;
  }
 }else if( b.type == NOTHING ){
  interp->errorMessage = "same?: expected two values to compare";  result = ERRORITEM(*p);
 }
 deleteItem( &a );  deleteItem( &b );  return result;
}

struct item  primitive_While( struct interp *interp, char **p ){
 char *test_p = *p;
 struct item test = getBoolean(interp,p);  if( test.type == ERROR ) return test;
 char *paren_p = *p;
 if( getItem(p).type != LPAREN ){
  interp->errorMessage = "while: syntax error: expected a code block in parens";
  return ERRORITEM(*p);
 }
 char *code_p = *p;
 char *end_p = NULL;
 struct item result; result.type = UNDEFINED;
 primitive_while_loop:
 if( test.data.number ){
  // tidy
  deleteItem( &result );
  // evaluate loop body
  result = eval_p( interp, p, 1 );  if( result.type == ERROR ) return result;
  if( ! end_p ){  end_p = *p;  }
  // test loop condition and loop
  *p = test_p;  test = getBoolean(interp,p);  if( test.type == ERROR ) {  deleteItem( &result );  return test;  }
  if( test.data.number ){  *p = code_p;  goto primitive_while_loop;  }
 }
 // loop completed, move past the loop body
 if( end_p ){
  *p = end_p;
 }else{
  *p = paren_p;  skipParen(p);
 }
 return result;
}

struct item  primitive_Foreach( struct interp *interp, char **p ){
 // foreach i array ()
 struct item varName = getItem(p);
 if( varName.type != SYMBOL ){
  interp->errorMessage = "foreach: expected var name";  return ERRORITEM(*p);
 }
 // get array
 struct item array = getValue( interp, p );  if( array.type == ERROR ) return array;
 switch( array.type ){
  case ARRAY: case UNDEFINED: break;
  default:  deleteItem(&array);  interp->errorMessage = "foreach: wrong type: expected an array or undefined item";  return ERRORITEM(*p);
 }
 char *paren_p = *p;
 if( getItem(p).type != LPAREN ){
  deleteItem( &array );  interp->errorMessage = "foreach: expected code block in parens";  return ERRORITEM(*p);
 }
 char *code_p = *p;
 if( array.type == UNDEFINED ){
  *p = paren_p;
  if( skipParen(p) ){  interp->errorMessage = "missing ')'";  return ERRORITEM(code_p);  }
  return UNDEFINEDITEM;
 }
 struct array *arr = array.data.array;
 // create loop variable
 struct var *var = installVar( interp, makeVar( UNDEFINEDITEM, varName.data.string ) );  struct item *itemPtr = var->value;
 var->flags = VARMANAGED;
 size_t i;
 struct item result;  result.type = UNDEFINED;
 for(  i = 0;  i < arr->size;  i++  ){
  storeItem( itemPtr, arr->itemArray + i, NULL );
  deleteItem( &result );  *p = code_p;  result = eval_p( interp, p, 1 );  if( result.type == ERROR ) break;
 }
 // complete the work: delete var and return the result
 deleteItem( &array );
 deleteVar(interp, var);
 return result;
}

struct item  primitive_For( struct interp *interp, char **p ){
 // examples:
 // for ( i 0 10 ) ()
 // for ( i 10 0 -1) ()
 if( getItem(p).type != LPAREN ){
  interp->errorMessage = "for: syntax error: expected params list in parens. example: 'for (varName startNumber untilNumber [stepNumber]) ([code])'";
  return ERRORITEM(*p);
 }
 struct item varName = getItem(p);
 if( varName.type != SYMBOL ){
  interp->errorMessage = "for: expected var name";  return ERRORITEM(*p);
 }
 struct item startNumber = getNumber(interp,p);  if( startNumber.type == ERROR ) return startNumber;
 struct item untilNumber = getNumber(interp,p);  if( untilNumber.type == ERROR ) return untilNumber;
 struct item stepNumber;  stepNumber.type = NUMBER;  stepNumber.data.number = 1;
 if( paramsRemain(p) ){
  stepNumber = getNumber(interp,p);  if( stepNumber.type == ERROR ) return stepNumber;
 }
 if( getItem(p).type != RPAREN ){
  interp->errorMessage = "for: syntax error: params list closing ')' missing";  return ERRORITEM(*p);
 }
 char *paren_p = *p;
 if( getItem(p).type != LPAREN ){
  interp->errorMessage = "for: syntax error: expected code block in parens";  return ERRORITEM(*p);
 }
 double until = untilNumber.data.number;  double step = stepNumber.data.number;
 char *code_p = *p;  char *end_p = NULL;  int sign = (step < 0) ^ (until<0)  ;
 // create loop variable
 struct var *var = installVar( interp, makeVar( startNumber, varName.data.string ) );  double *varP = &var->value->data.number;
 var->flags = VARMANAGED;
 // loop
 struct item result; result.type = UNDEFINED;
 while( *varP != until && ((*varP < until)^sign) ){
  *p = code_p;  deleteItem( &result );  result = eval_p( interp, p, 1 );  end_p = *p;
  if( result.type == ERROR ) break;
  *varP += step;
 }
 // complete the work: delete var and return the result
 deleteVar(interp, var);
 if( result.type == ERROR ) return result;
 if( ! end_p ){
  *p = paren_p;  skipParen(p);
 }
 return result;
}

struct item primitive_Delete( struct interp *interp, char **p ){
 struct item symbol = getItem(p);
 if( symbol.type != SYMBOL ){  interp->errorMessage = "delete: expected name of variable to delete";  return ERRORITEM(*p);  }
 struct var *var = lookupVarByName( interp, &symbol.data.string );
 if( !var ) return UNDEFINEDITEM;
 if( var->flags ){
  interp->errorMessage = "delete: can't delete this variable, it's protected or managed";  return ERRORITEM(*p);
 }
 deleteVar( interp, var );
 return NUMBERITEM(1);
}

struct string stringCat( struct string *a, struct string *b ){
 struct string out;  out.refCount = malloc(sizeof(unsigned int));  *out.refCount = 1;
 out.length = a->length + b->length;  out.s = calloc( out.length+1, sizeof(char) );
 memcpy( out.s, a->s, a->length );  memcpy( out.s+a->length, b->s, b->length );
 return out;
}

struct item primitive_CatS( struct interp *interp, char **p ){
 struct item  a = getString_AllowUndefinedItem( interp,p );  if( a.type == ERROR ) return a;
 struct item  b = getString_AllowUndefinedItem( interp,p );  if( b.type == ERROR ){  deleteItem(&a);  return b;  }
 struct item  c;  c.type = STRING;  c.data.string = stringCat( &a.data.string, &b.data.string );
 deleteItem(&a);  deleteItem(&b);
 while( paramsRemain(p) ){
  a = getString_AllowUndefinedItem( interp,p );  if( a.type == ERROR ){  deleteItem(&c);  return a;  }
  b.data.string = stringCat( &c.data.string, &a.data.string );  deleteItem(&c);  deleteItem(&a);  c = b;
 }
 return c;
}

struct item primitive_StrS( struct interp *interp, char **p ){
 struct item n = getNumber( interp,p );  if( n.type == ERROR ) return n;
 char numberString[256];  unsigned int length;
 if( ((double)(long long int)n.data.number != n.data.number) || ( (n.data.number==n.data.number) && (n.data.number!=n.data.number) ) ) {
  length = snprintf( numberString, 256, "%.16f", n.data.number );
 }else{
  length = snprintf( numberString, 256, "%lld", (long long int) n.data.number );
 }
 n.type = STRING;  n.data.string = charPtrToNewString( numberString, length );
 return n; 
}

struct item primitive_ValS( struct interp *interp, char **p ){
 struct item n = getString( interp,p );  if( n.type == ERROR ) return n;
 char *endptr;
 double number = strtod( n.data.string.s, &endptr );
 deleteItem(&n);
 n.type = NUMBER;  n.data.number = number;
 return n;
}

struct item primitive_ChrS( struct interp *interp, char **p ){
 struct item n = getNumber( interp,p );  if( n.type == ERROR ) return n;
 char s[2];  s[1] = 0;  s[0] = n.data.number;
 n.type = STRING;  n.data.string = charPtrToNewString( s, 1 );
 return n;
}

struct item  primitive_ByteSet( struct interp *interp, char **p ){
 struct item result;  result.type = NUMBER;
 struct item  s = getString( interp, p );  if( s.type == ERROR ) return s;
 if( ! s.data.string.refCount ){  interp->errorMessage = "byte-set: can't modify read-only string";  return ERRORITEM(*p);  }
 struct item  index = getNumber( interp, p );  if( index.type == ERROR ){  result = index;  goto ByteSet_exit;  }
 struct item  value = getNumber( interp, p );  if( value.type == ERROR ){  result = value;  goto ByteSet_exit;  }
 long long int indexV = index.data.number;
 if( indexV < 0 || indexV >= s.data.string.length ){
  fprintf( stderr, "index is %lld (%f), length is %u\n", indexV, index.data.number, s.data.string.length);  interp->errorMessage = "byte-set: index out of range";  result = ERRORITEM(*p);
 }else{
  result.data.number = ( *(unsigned char*)(s.data.string.s + indexV) = value.data.number ); 
 }
 ByteSet_exit:  deleteItem( &s );  return result;
}

struct item  primitive_ByteGet( struct interp *interp, char **p ){
 struct item result;  result.type = NUMBER;
 struct item  s = getString( interp, p );  if( s.type == ERROR ) return s;
 struct item  index = getNumber( interp, p );  if( index.type == ERROR ){  deleteItem( &s );  return s;  }
 long long int indexV = index.data.number;
 if( indexV < 0 || indexV >= s.data.string.length ){
  interp->errorMessage = "byte-get: index out of range";  result = ERRORITEM(*p);
 }else{
  result.data.number = *(unsigned char*)( s.data.string.s + indexV );
 }
 deleteItem( &s );  return result;
}

struct item  primitive_StringS( struct interp *interp, char **p ){
 struct item  n = getNumber( interp, p );  if( n.type == ERROR ) return n;
 struct item  s = getString( interp, p );  if( s.type == ERROR ) return s;
 long long int reps = n.data.number;
 if( reps <= 0 || s.data.string.length == 0 ){  deleteItem( &s );  s.data.string.refCount = NULL;  s.data.string.length = 0;  return s;  }
 size_t size = 1 + (size_t)reps * (size_t)s.data.string.length;
 if( size != (unsigned int)size ){  deleteItem( &s );  interp->errorMessage = "string$: string too big";  return ERRORITEM(*p);  }
 char *str = malloc( size );
 if( ! str ){  deleteItem( &s );  interp->errorMessage = "string$: couldn't create string";  return ERRORITEM(*p);  }
 unsigned int length = size - 1;  str[length] = 0;
 char *ptr = str;
 for(  unsigned int i = 0;  i < reps;  i++ ){
  memcpy( ptr, s.data.string.s, s.data.string.length );
  ptr += s.data.string.length;
 }
 deleteItem( &s );
 struct item out;  out.type = STRING;  out.data.string = heapCharPtrToString( str, length );
 return out;
}

struct item primitive_AscS( struct interp *interp, char **p ){
 struct item s = getString( interp,p );  if( s.type == ERROR ) return s;
 struct item out;  out.type = NUMBER;  out.data.number = *(unsigned char*)s.data.string.s;
 deleteItem( &s );
 return out;
}

struct item primitive_RangeS( struct interp *interp, char **p ){
 struct item s = getString(interp,p);  if( s.type == ERROR ) return s;
 struct item a = getNumber(interp,p);  if( a.type == ERROR ){  deleteItem( &s );  return a;  }
 struct item b;
 long long int index1 = a.data.number, index2 = index1;  unsigned int length = s.data.string.length;
 if( paramsRemain(p) ){  b = getNumber(interp,p);  if( b.type == ERROR ){  deleteItem( &s );  return b;  }  index2 = b.data.number;  }
 if( ! length || index1>index2 ){
  deleteItem( &s );  s.data.string = (struct string){NULL,0,NULL};  return s;
 }
 if( index1 >= length ) index1 = length-1;
 if( index1 < 0 ) index1 = 0;
 if( index2 >= length ) index2 = length-1;
 if( index2 < 0 ) index2 = 0;
 struct item out;  out.type = STRING;  out.data.string = charPtrToNewString( s.data.string.s + index1, index2-index1+1 );
 deleteItem( &s );
 return out;
}

struct item primitive_EqualS( struct interp *interp, char **p ){
 struct item a = getString(interp,p);  if( a.type == ERROR ) return a;
 struct item b = getString(interp,p);  if( b.type == ERROR ){  deleteItem(&a);  return b;  }
 struct item out;  out.type = NUMBER;
 if( !a.data.string.length || !b.data.string.length )
  out.data.number = ( a.data.string.length == b.data.string.length );
 else
  out.data.number = stringsMatch( &a.data.string, &b.data.string );
 deleteItem( &a );  deleteItem( &b );
 return out;
}

struct item primitive_CompareS( struct interp *interp, char **p ){
 struct item a = getString(interp,p);  if( a.type == ERROR ) return a;
 struct item b = getString(interp,p);  if( b.type == ERROR ){  deleteItem(&a);  return b;  }
 struct item out;  out.type = NUMBER;  out.data.number = 0;
 if( ! a.data.string.length ^ ! b.data.string.length ){
  if( a.data.string.length ){
   out.data.number = *(unsigned char*)a.data.string.s;
  }else{
   out.data.number = -(int)*(unsigned char*)b.data.string.s;
  }
 }else{
  unsigned int i;  unsigned char *aa = (unsigned char *)a.data.string.s,  *bb = (unsigned char *)b.data.string.s,  l;
  l = a.data.string.length < b.data.string.length ? a.data.string.length : b.data.string.length;
  for(  i = 0;  i < l;  i++  ){
   if( aa[i] != bb[i] ){  out.data.number = aa[i] - bb[i];  break;  }
  }
 }
 deleteItem( &a );  deleteItem( &b );
 return out;
}

struct item primitive_Fract( struct interp *interp, char **p ){
 struct item n = getNumber( interp,p ); if( n.type == ERROR ) return n;
 n.data.number = n.data.number - (long long int) n.data.number;
 return n;
}

struct item primitive_Array( struct interp *interp, char **p ){
 int nDims = 0; unsigned int dims[8];
 while( paramsRemain(p) ){
  if( nDims >= 8 ){  interp->errorMessage = "array: too many dimensions";  return ERRORITEM(*p);  }
  struct item dim = getValue(interp,p);
  if( dim.type != NUMBER ){  deleteItem( &dim );  interp->errorMessage = "array: syntax error: expected number for dimension";  return ERRORITEM(*p);  }
  if( dim.data.number < 0 || dim.data.number > 4294967296.0 ){
   interp->errorMessage = "array: bad dimension";  return ERRORITEM(*p);
  }
  dims[nDims] = dim.data.number;
  nDims ++;
 }
 if( ! nDims ){  return UNDEFINEDITEM;  /*interp->errorMessage = "array: syntax error: can't make an array with no dimensions";  return ERRORITEM(*p);*/  }
 struct array *array = calloc( 1, sizeof(struct array) );
 array->nDims = nDims;  array->dims = calloc( nDims, sizeof(unsigned int) );  array->refCount = 1;
 size_t arraySize = 0;
 int i; for( i=0; i<nDims; i++){
  array->dims[i] = dims[i];  arraySize = arraySize * dims[i] + dims[i];
 }
 array->size = arraySize;
 array->itemArray = calloc( arraySize, sizeof(struct item) );
 if( ! array->itemArray ){
  free( array->dims );  free( array );
  interp->errorMessage = "failed to create array";  return ERRORITEM(*p);
 }
 struct item out;  out.type = ARRAY;  out.data.array = array;  return out;
}

struct item  primitive_ItemType( struct interp *interp, char **p ){
 struct item item = getValue( interp, p );  if( item.type == ERROR ) return item;
 deleteItem( &item );  int type = item.type;
 item.type = NUMBER;  item.data.number = type;  return item;
}

struct item primitive_MakeArray( struct interp *interp, char **p ){
 struct array *array = NULL;  struct item arr;  arr.type = ARRAY;
 struct item value;
 while( paramsRemain(p) ){
  value = getValue(interp,p);  if( value.type == ERROR) goto primitive_makearray_failure;
  if( ! array ){
   array = makeSimpleArray( &value );
  }else{
   appendItemToSimpleArray( array, &value );
  }
 }
 if( ! array ) return UNDEFINEDITEM;
 arr.data.array = array;
 return arr;
 primitive_makearray_failure:
 if( array ){  arr.data.array = array;  deleteItem( &arr );  }
 return value;
}

struct item  newCopyOfArray( struct interp *interp, struct item a ){
 if( a.type == UNDEFINED ) return a;
 if( a.type != ARRAY ){ printf( "fuck you\n" ); exit(1); }
 struct array *aa = a.data.array;
 struct array *bb = malloc( sizeof( struct array ) );
 bb->refCount = 1;  bb->chainRefCount = 0;  bb->arrayContainsMemoryResources = 0;  bb->nDims = aa->nDims;  bb->size = aa->size;
 bb->dims = malloc( sizeof(unsigned int)*aa->nDims );  for(  int i=0;  i < aa->nDims;  i++  ){  bb->dims[i] = aa->dims[i];  }
 bb->itemArray = calloc( bb->size, sizeof(struct item) );
 if( ! bb->itemArray ){
  free( bb->dims );  free( bb );
  interp->errorMessage = "newCopyOfArray: malloc failed";  return ERRORITEM(NULL);
 }
 for(  size_t i = 0;  i < bb->size;  i++  ){
  storeItem( bb->itemArray + i, aa->itemArray + i, bb );
 }
 struct item out;  out.type = ARRAY;  out.data.array = bb;  return out;
}

struct item _primitive_Join_getSingleDimensionalArrayOrUndefinedItem( struct interp *interp, char **p ){
 struct item a = getValue(interp,p);  if( a.type == ERROR ) return a;
 switch( a.type ){
  case UNDEFINED:
   return a;
  case ARRAY:
   if( a.data.array->nDims == 1 ){
    return a;
   }
  default:
   deleteItem( &a );  interp->errorMessage = "join: expected 1-dimensional array";  return ERRORITEM(*p);
 }
}

struct item primitive_Join( struct interp *interp, char **p ){
 // 'join' takes two 1D arrays and creates a new array consisting of the two parts joined together
 struct item a = _primitive_Join_getSingleDimensionalArrayOrUndefinedItem( interp, p );  if( a.type == ERROR ) return a;
 struct item b = _primitive_Join_getSingleDimensionalArrayOrUndefinedItem( interp, p );  if( b.type == ERROR ){  deleteItem( &a );  return b;  }
 if( a.type == UNDEFINED && b.type == UNDEFINED ) return UNDEFINEDITEM;
 if( a.type == UNDEFINED || b.type == UNDEFINED ){
  struct item result = newCopyOfArray( interp, a.type == UNDEFINED ? b : a );
  if( result.type == ERROR ) result = ERRORITEM(*p);
  deleteItem( a.type == UNDEFINED ? &b : &a );
  return result;
 }
 struct array *aa = a.data.array, *bb = b.data.array, *cc = calloc( 1, sizeof(struct array) );
 cc->refCount = 1;  cc->nDims = 1;  cc->dims = calloc( 1, sizeof(unsigned int) );  cc->size = aa->size + bb->size;  cc->itemArray = calloc( cc->size, sizeof(struct item) );
 cc->dims[0] = cc->size;
 unsigned int i;
 for( i=0;  i < aa->size;  i++ ){  storeItem( cc->itemArray + i, aa->itemArray + i, cc );  }
 for( i=0;  i < bb->size;  i++ ){  storeItem( cc->itemArray + i + aa->size, bb->itemArray + i, cc );  }
 deleteItem( &a );  deleteItem( &b );
 a.data.array = cc;  return a;
}

struct item primitive_Append( struct interp *interp, char **p ){
 struct item a = _primitive_Join_getSingleDimensionalArrayOrUndefinedItem( interp, p );  if( a.type == ERROR ) return a;
 struct item b = getValue( interp, p );  if( b.type == ERROR ){  deleteItem( &a );  return b;  }
 if( a.type == UNDEFINED ){
  struct item out;  out.type = ARRAY;  out.data.array = makeSimpleArray( &b );  /*deleteItem( &b );*/
  return out;
 }else{
  appendItemToSimpleArray( a.data.array, &b );  /*deleteItem( &b );*/
  return a;
 }
}

struct item  primitive_Dimensions( struct interp *interp, char **p ){
 struct item out;  out.type = NUMBER;  struct item array = getValue(interp,p);
 if( array.type == UNDEFINED ){  out.data.number = 0;  return out;  }
 if( array.type != ARRAY ){  deleteItem( &array );  interp->errorMessage = "dimensions: expected an array";  return ERRORITEM(*p);  }
 out.data.number = array.data.array->nDims;  deleteItem( &array );  return out;
}

struct item  primitive_Length( struct interp *interp, char **p ){
 struct item item = getValue(interp,p);
 unsigned int result;
 switch( item.type ){
  case UNDEFINED: result = 0; break;
  case STRING: result = item.data.string.length; break;
  case ARRAY:  result = item.data.array->dims[0]; break;
  default:  deleteItem( &item );  interp->errorMessage = "length: invalid type";  return ERRORITEM(*p);
 }
 deleteItem( &item );
 struct item out;  out.type = NUMBER;  out.data.number = result;  return out;
}

struct item  primitive_Error( struct interp *interp, char **p ){
 struct item result = ERRORITEM(*p);
 struct item message = getString(interp,p);  if( message.type == ERROR ) return message;
 unsigned int l = message.data.string.length >= ERROR_MESSAGE_BUFFER_SIZE ? ERROR_MESSAGE_BUFFER_SIZE-1 : message.data.string.length;
 unsigned int i;  for( i=0; i<l; i++){ interp->errorMessageBuffer[i] = message.data.string.s[i]; }  interp->errorMessageBuffer[i]=0;
 deleteItem( &message );  interp->errorMessage = interp->errorMessageBuffer;
 return result;
}

struct item  primitive_ErrorMessage( struct interp *interp, char **p ){
 struct item out;  out.type = STRING;  out.data.string = charPtrToNewString( interp->errorMessage, strlen(interp->errorMessage) );  return out;
}

struct item  primitive_Catch( struct interp *interp, char **p ){
 // catch ( code block in parens ) [optional value return variable name] ;
 // returns 0 if all okay, returns 1 if an error occurred
 // causes an error if no code block in parens or the parens are not matched
 char *paren_p = *p;
 if( getItem(p).type != LPAREN ){  interp->errorMessage = "catch: expected code block in parens";  return ERRORITEM(*p);  }
 char *code_p = *p;
 *p = paren_p;  if( skipParen(p) ){  interp->errorMessage = "catch: missing ')'";  return ERRORITEM(*p);  }
 struct item result = eval_( interp, code_p, 1 );  if( result.type == ERROR && interp->errorMessage == returnExceptionMsg ){  result = interp->returnValue;  interp->returnValue.type = 0;  interp->errorMessage = "retexp";  }
 int anErrorOccurred = (result.type == ERROR);
 struct item *returnValuePtr = NULL;  struct array *returnValuePtrParentArray = NULL;
 // optional value return in variable passed by reference
 if( paramsRemain(p) ){
  struct item symbol = getItem(p);
  if( symbol.type != SYMBOL ){  deleteItem( &result );  interp->errorMessage = "catch: expected var name for optional return value";  return ERRORITEM(*p);  }
  int infoReturn = 0;  returnValuePtr = lookupItemPtr( interp, p, &symbol, &infoReturn, &returnValuePtrParentArray  );
  if( infoReturn ) return symbol;
  if( ! returnValuePtr && ! anErrorOccurred ) returnValuePtr = installVar( interp, makeVar( UNDEFINEDITEM, symbol.data.string ) )->value;
 }
 // store result, but only if the result was not an error
 if( returnValuePtr && ! anErrorOccurred ){
  storeItem( returnValuePtr, &result, returnValuePtrParentArray );
 }
 deleteItem( &result );
 result.type = NUMBER;  result.data.number = anErrorOccurred;  return result;
}

struct item  primitive_Eval( struct interp *interp, char **p ){
 struct item str = getValue(interp,p);  if( str.type == ERROR ) return str;
 if( str.type != STRING ){  deleteItem( &str );  interp->errorMessage = "eval: expected program string";  return ERRORITEM(*p);  }
 if( ! str.data.string.length ){
  deleteItem( &str );  return UNDEFINEDITEM;
 }
 if( ! str.data.string.refCount ){
  struct item new;  new.type = STRING;  new.data.string = charPtrToNewString( str.data.string.s, str.data.string.length );
  deleteItem( &str );
  str = new;
 }
 struct item result = eval( interp, str.data.string.s );
 if( result.type == NOTHING ) result.type = UNDEFINED;
 deleteItem( &str );
 return result;
}

struct item  primitive_Wait( struct interp *interp, char **p ){
 struct item miliseconds = getNumber( interp, p );  if( miliseconds.type == ERROR ) return miliseconds;
 usleep( 1000 * miliseconds.data.number );
 miliseconds.type = UNDEFINED;  return miliseconds;
}

#define FRACT(x) ((x)-(int)(x))
struct item  primitive_Rnd( struct interp *interp, char **p ){
 double x = 1.0;  struct item item;  item.type = NUMBER;
 if( paramsRemain(p) ){
  item = getNumber(interp,p);  if( item.type == ERROR ) return item;
  x = item.data.number;
 }
 interp->rndSeed = FRACT(interp->rndSeed + 0.6456632118884769);
 double v = x * FRACT( interp->rndSeed * 66160.2209630171 * FRACT( interp->rndSeed * 54833.72553941885 * FRACT( interp->rndSeed * 90425.14110468594 ) ) );
 item.data.number = v;
 return item;
}
struct item  primitive_SetRndSeed( struct interp *interp, char **p ){
 struct item item = getNumber(interp,p);  if( item.type == ERROR ) return item;
 double v = FRACT(item.data.number);  if( v < 0 ) v = -v;
 interp->rndSeed = v;
 return UNDEFINEDITEM;
}
struct item  primitive_CurrentRndSeed( struct interp *interp, char **p ){
 struct item item;  item.type = NUMBER;  item.data.number = interp->rndSeed;  return item;
}
#include <sys/time.h>
double newRndSeed(){
 struct timeval tv;  gettimeofday(&tv, NULL); 
 double v = FRACT( FRACT( tv.tv_sec * 0.0158128735792184978174 ) + tv.tv_usec * 0.199151828397248156 );
 return FRACT( v * 4444.1571889687192874 * FRACT( v * 5188.59238759872498 ) );
}
#undef FRACT
struct item  primitive_NewRndSeed( struct interp *interp, char **p ){
 double v = newRndSeed();  interp->rndSeed = v;
 struct item item;  item.type = NUMBER;  item.data.number = v;  return item;
}

struct item  primitive_Quit( struct interp *interp, char **p ){
 if( paramsRemain(p) ){
  struct item exitCode = getNumber(interp,p);  if( exitCode.type == ERROR ) return exitCode;
  exit( (int) exitCode.data.number );
 }
 exit(0);
}

struct item primitive_StringInParens( struct interp *interp, char **p ){
 if( peekItem(p).type != LPAREN ){
  interp->errorMessage = "string-in-parens: expected parens. The contents of the parens will be returned as a string";  return ERRORITEM(*p);
 }
 char *start = *p;  getItem(&start);
 if( skipParen(p) ){
  interp->errorMessage = "string-in-parens: missing ')'";  return ERRORITEM(start);
 }
 char *end = *p - 1;
 struct item out;  out.type = STRING;  out.data.string = (struct string){ NULL, end-start, start };  return out;
}

struct item primitive_System( struct interp *interp, char **p ){
 struct item cmd = getNullTerminatedString(interp,p);  if( cmd.type == ERROR ) return cmd;
 struct item returnValue;  returnValue.type = NUMBER;  returnValue.data.number = system( cmd.data.string.s );
 deleteItem( &cmd );  return returnValue;
}

struct interp*  makeInterp( struct interp *interp ){
 if( ! interp ) interp = calloc( 1, sizeof( struct interp ) );
 interp->errorMessage = "(C)2024 Rubish";
 interp->rndSeed = newRndSeed();
 installPrimitive( interp, primitive_InputReady,	"input-ready?");
 installPrimitive( interp, primitive_System,		"system");
 installPrimitive( interp, primitive_Same,		"same?");
 installPrimitive( interp, primitive_NewRndSeed, 	"get-new-rnd-seed" );
 installPrimitive( interp, primitive_StringInParens,	"string-in-parens" );
 installPrimitive( interp, primitive_Delete,		"delete" );
 installPrimitive( interp, primitive_Quit,		"quit");
 installPrimitive( interp, primitive_Source,		"source" );
 installPrimitive( interp, primitive_ByteSet,		"byte-set" );
 installPrimitive( interp, primitive_ByteGet,		"byte-get" );
 installPrimitive( interp, primitive_StringS,		"string$" );
 installManagedFileport( interp, stderr, FPWRITE,	"stderr" );
 installManagedFileport( interp, stdin,  FPREAD,	"stdin" );
 installManagedFileport( interp, stdout, FPWRITE,	"stdout" );
 installPrimitive( interp, primitive_Openin,		"openin" );
 installPrimitive( interp, primitive_Openout,		"openout" );
 installPrimitive( interp, primitive_Openup,		"openup" );
 installPrimitive( interp, primitive_FileFlush,		"file-flush" );
 installPrimitive( interp, primitive_FileRead,		"file-read" );
 installPrimitive( interp, primitive_FileWrite,		"file-write" );
 installPrimitive( interp, primitive_FileSize,		"file-size" );
 installPrimitive( interp, primitive_FileEof,		"file-eof" );
 installPrimitive( interp, primitive_FileTell,		"file-tell" );
 installPrimitive( interp, primitive_FileSeekTo,	"file-seek-to" );
 installPrimitive( interp, primitive_FileSeekFrom,	"file-seek-from-cur" );
 installPrimitive( interp, primitive_FileSeekFromEnd,	"file-seek-from-end" );
 installPrimitive( interp, primitive_FilePutChar,	"put-char" );
 installPrimitive( interp, primitive_FileGetLine,	"get-line" );
 installPrimitive( interp, primitive_FileGetByte,	"get-byte" );
 installPrimitive( interp, primitive_FileGetChar,	"get-char" );
 installPrimitive( interp, primitive_Print,		"print" );
 installPrimitive( interp, primitive_Dimensions,	"dimensions" );
 installPrimitive( interp, primitive_Error,		"error" );
 installPrimitive( interp, primitive_Catch,		"catch" );
 installPrimitive( interp, primitive_Return,		"return" );
 installPrimitive( interp, primitive_ErrorMessage,	"error-message" );
 installPrimitive( interp, primitive_Eval,		"eval" );
 installPrimitive( interp, primitive_ItemType,		"item-type" );
 installPrimitive( interp, primitive_Func,		"func" );
 installPrimitive( interp, primitive_Array,		"array" );
 installPrimitive( interp, primitive_Wait,		"wait" );
 installPrimitive( interp, primitive_CompareS,		"compare$" );
 installPrimitive( interp, primitive_CatS,		"cat$" );
 installPrimitive( interp, primitive_RangeS,		"range$" );
 installPrimitive( interp, primitive_AscS,		"asc$" );
 installPrimitive( interp, primitive_ChrS,		"chr$" );
 installPrimitive( interp, primitive_StrS,		"str$" );
 installPrimitive( interp, primitive_ValS,		"val$" );
 installPrimitive( interp, primitive_EqualS,		"equal$" );
 installPrimitive( interp, primitive_Length,		"length" );
 installPrimitive( interp, primitive_CurrentRndSeed,	"current-rnd-seed");
 installPrimitive( interp, primitive_SetRndSeed,	"set-rnd-seed");
 installPrimitive( interp, primitive_Rnd,		"rnd");
 installPrimitive( interp, primitive_Int,		"int" );
 installPrimitive( interp, primitive_Abs,		"abs" );
 installPrimitive( interp, primitive_Sgn,		"sgn" );
 installPrimitive( interp, primitive_Foreach,		"foreach");
 installPrimitive( interp, primitive_Fract,		"fract" );
 installPrimitive( interp, primitive_For,		"for"	);
 installPrimitive( interp, primitive_While,		"while" );
 installPrimitive( interp, primitive_Local,		"local" );
 installPrimitive( interp, primitive_MakeArray,		"make-array" );
 installPrimitive( interp, primitive_Join,		"join" );
 installPrimitive( interp, primitive_Append,		"append" );
 installPrimitive( interp, primitive_ShiftLeft,		"<<" );
 installPrimitive( interp, primitive_ShiftRight,	">>" );
 installPrimitive( interp, primitive_ShiftShiftRight,	">>>" );
 installPrimitive( interp, primitive_BitwiseAnd,	"&" );
 installPrimitive( interp, primitive_BitwiseOr,		"|" );
 installPrimitive( interp, primitive_BitwiseXor,	"^" );
 installPrimitive( interp, primitive_NotEqual,		"!=" );
 installPrimitive( interp, primitive_Equal,		"=" );
 installPrimitive( interp, primitive_Less,		"<" );
 installPrimitive( interp, primitive_LessEqual,		"<=" );
 installPrimitive( interp, primitive_Greater,		">" );
 installPrimitive( interp, primitive_GreaterEqual,	">=" );
 installPrimitive( interp, primitive_Neg,		"neg" );
 installPrimitive( interp, primitive_BitwiseNot,	"~" );
 installPrimitive( interp, primitive_And,		"&&" );
 installPrimitive( interp, primitive_Or,		"||" );
 installPrimitive( interp, primitive_Modulo,		"%" );
 installPrimitive( interp, primitive_Not,		"!" );
 installPrimitive( interp, primitive_Set,		"set" );
 installPrimitive( interp, primitive_If,		"if" );
 installPrimitive( interp, primitive_Mul,		"*" );
 installPrimitive( interp, primitive_Div,		"/" );
 installPrimitive( interp, primitive_Add,		"+" );
 installPrimitive( interp, primitive_Sub,		"-" );
 return interp;
}

void deleteInterp( struct interp *interp ){
 if( interp->returnValue.type ) deleteItem( &interp->returnValue );
 while( interp->vars ){  deleteVar(interp, interp->vars);  }
 free( interp );
}

// evaluate and advance 'p'
struct item  eval_p( struct interp *interp, char **p, int inParens ){
 struct item  value;  value.type = NOTHING;
 struct item  peekNext;
 //if( inParens && getItem(p).type != LPAREN ){  interp->errorMessage = "this hapened";  return ERRORITEM(*p);  }
 while(1){
  deleteItem( &value );
  value = getValue( interp, p );
  if( value.type == ERROR ) return value;
  eval_check_next: ;
  char *start = *p;  peekNext = getItem( p );
  switch( peekNext.type ){
   case NOTHING: case RPAREN: goto eval_p_exit;
   case STOP: goto eval_check_next;
   default: *p = start;
  }
 }
 eval_p_exit:
 if( inParens ){
  if( peekNext.type != RPAREN ){
   interp->errorMessage = "missing ')'";  deleteItem( &value );  return ERRORITEM(*p);
  }
 }else{
  if( peekNext.type == RPAREN ){
   interp->errorMessage = "extraneous/unmatched ')'";  deleteItem( &value );  return ERRORITEM(*p);
  }
 }
 if( value.type == NOTHING ) value.type = UNDEFINED;
 return value;
}

// evaluate without advancing 'p'
struct item  eval( struct interp *interp, char *text ){
 return eval_p( interp, &text, 0 );
}

struct item  eval_( struct interp *interp, char *text, int inParens ){
 return eval_p( interp, &text, inParens );
}

int fileExists( char *filename ){
 FILE *fp = fopen( filename,"rb" );
 if( fp ) fclose(fp);
 return !!fp;
}

long fileSize( FILE *fp ){
 long now = ftell( fp );
 fseek( fp, 0, SEEK_END );
 long end = ftell( fp );
 fseek( fp, now, SEEK_SET );
 return end;
}

char* readTextFile( char *filename ){
 FILE *fp = fopen( filename,"rb" );  if( ! fp ) return NULL;
 size_t size = fileSize( fp );  char *out = malloc( size+1 );  out[size]=0;
 size_t result = fread( out, 1, size, fp );
 if( result != size ) fprintf( stderr, "oh no: readTextFile\n" );
 fclose( fp );
 return out;
}

struct item  charPtrsToArray( int argc, char **argv ){
 if( argc <= 0 ) return UNDEFINEDITEM;
 int i;  struct array *array = NULL;
 for(  i = 0;  i < argc;  i++  ){
  struct item stringItem;  stringItem.type = STRING;  stringItem.data.string = charPtrToNewString( argv[i], strlen(argv[i]) );
  if( array )
   appendItemToSimpleArray( array, &stringItem );
  else
   array = makeSimpleArray( &stringItem );
 }
 struct item out;  out.type = ARRAY;  out.data.array = array;  return out;
}

void installStringArray( struct interp *interp, int argc, char **argv, char *name ){
 struct item command_line = charPtrsToArray( argc, argv );  if( command_line.type == ARRAY ) command_line.data.array->refCount = 0;
 installVar( interp, makeVar( command_line, charPtrToString( name ) ) );
}

void findLineColumn( char *text, char *p ){
 unsigned int line = 1, column = 0;
 while( *text ){
  switch( *text ){
   case '\n': line += 1; column = 0;
  }
  if( text == p ){ 
   break;
  }
  text += 1;  column += 1;
 }
 fprintf( stderr, "At line %u, column %u\n", line, column );
}

void printLineColumnInfo( struct interp *interp, char *filename, char *text, char *p ){
 unsigned int fileLength = 0;
 if( filename && text ){
  fileLength = strlen( text );
 }
 if( p >= text && p <= text+fileLength ){
  fprintf( stderr, "In file '%s'\n", filename );
  findLineColumn( text, p );
  return;
 }
 struct var *var = interp->vars;
 while( var ){
  if( var->value->type == FUNCTION && ! var->value->data.func->primitive ){
   struct func *func = var->value->data.func;  if( p < func->body ){  var = var->next;  continue;  }
   unsigned int length = strlen( func->body );  if( ! (  p >= func->body  &&  p <= func->body + length  ) ){  var = var->next;  continue;  }
   fprintf( stderr, "In function '" );  printString( stderr, &var->name );  fprintf( stderr, "'\n" );
   findLineColumn( func->body, p );
   return;
  }
  var = var->next;
 }
}

struct item  evalTextFile( struct interp *interp, char *filename, int printInfoOnError ){
 char *text = readTextFile( filename );  if( ! text ){  interp->errorMessage = "couldn't read source file";  return ERRORITEM(NULL);  }
 struct item result = eval( interp, text );
 if( result.type == ERROR && interp->errorMessage == returnExceptionMsg ){  result = interp->returnValue;  interp->returnValue.type = 0;  interp->errorMessage = "retexp";  }
 if( printInfoOnError && (result.type == ERROR) ){
  // try to find out where the error occured, to print line and column info if possible
  printLineColumnInfo( interp, filename, text, (char*)result.data.ptr );
 }
 free( text );
 return result;
}

struct item  primitive_Source( struct interp *interp, char **p ){
 struct item filepath = getNullTerminatedString(interp,p);  if(filepath.type == ERROR) return filepath;
 struct item result = evalTextFile( interp, filepath.data.string.s, 0 );
 deleteItem( &filepath );
 return result;
}

#ifdef INCLUDE_PROMPT
void Rubish_prompt( struct interp *interp ){
 #include "prompt.c"
 printf( "Rubish prompt\n" );
 eval( interp, (char*) prompt_rubish );
}
#endif

struct item  Rubish_main( struct interp *interp, int argc, char **argv ){
 installStringArray( interp, argc-2, argv+2, "command-line" );
 struct item result;
 // get and run program
 if( argc == 1 ){
  fprintf( stderr, "Rubish interpreter\nhttps://github.com/dusthillresident/Rubish\nUsage: %s [program text or path to program text file] ([arguments to program])\n", argv[0] );
  #ifdef INCLUDE_PROMPT
  Rubish_prompt( interp );
  #endif
  return UNDEFINEDITEM;
 }else if( argc >= 2 ){
  if( fileExists(argv[1]) ){
   result = evalTextFile( interp, argv[1], 1 );
  }else{
   result = eval( interp, argv[1] );
   if( result.type == ERROR ){
    printLineColumnInfo( interp, "(command line input)", argv[1], (char*)result.data.ptr );
   }
  }
 }
 // print result
 if( result.type == ERROR ){
  fprintf( stderr, "-- Error result --\n" );
  fprintf( stderr, "Error message: %s\n", interp->errorMessage );
 }
 #if MAIN_PRINT_RESULT
 else{
  fprintf( stderr, "-- Good result --\n");
  fprintf( stderr, "Result:\n");
  printItem( stderr, result );
  fprintf( stderr, "\n" );
 }
 #endif
 return result;
}

#ifndef RUBISH_NOT_STANDALONE
int main(int argc, char **argv){
 struct interp *interp = makeInterp(NULL);
 struct item result = Rubish_main( interp, argc, argv ); 
 deleteItem( &result );
 deleteInterp( interp );
 if( result.type == NUMBER )
  return (int) result.data.number;
 else
  return 0;
}
#endif
