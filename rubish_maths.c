
#include <math.h>

#define MATHSLIB_PRIMITIVE_1( NAME ) \
struct item  primitive_##NAME( struct interp *interp, char **p ){ \
 struct item  a = getNumber( interp, p );  if( a.type == ERROR )  return a; \
 a.data.number = NAME ( a.data.number );  return a; \
} 

MATHSLIB_PRIMITIVE_1( sin   ) 
MATHSLIB_PRIMITIVE_1( cos   )
MATHSLIB_PRIMITIVE_1( tan   )
MATHSLIB_PRIMITIVE_1( asin  )
MATHSLIB_PRIMITIVE_1( acos  )
MATHSLIB_PRIMITIVE_1( atan  )
MATHSLIB_PRIMITIVE_1( asinh )
MATHSLIB_PRIMITIVE_1( acosh )
MATHSLIB_PRIMITIVE_1( atanh )
MATHSLIB_PRIMITIVE_1( cbrt  )
MATHSLIB_PRIMITIVE_1( ceil  )
MATHSLIB_PRIMITIVE_1( cosh  )
MATHSLIB_PRIMITIVE_1( exp   )
MATHSLIB_PRIMITIVE_1( exp2  )
MATHSLIB_PRIMITIVE_1( expm1 )
MATHSLIB_PRIMITIVE_1( erf   )
MATHSLIB_PRIMITIVE_1( erfc  )
MATHSLIB_PRIMITIVE_1( fabs  )
MATHSLIB_PRIMITIVE_1( floor )
MATHSLIB_PRIMITIVE_1( ilogb )
MATHSLIB_PRIMITIVE_1( lgamma  )
MATHSLIB_PRIMITIVE_1( llrint  )
MATHSLIB_PRIMITIVE_1( llround )
MATHSLIB_PRIMITIVE_1( log )
MATHSLIB_PRIMITIVE_1( log10 )
MATHSLIB_PRIMITIVE_1( log1p )
MATHSLIB_PRIMITIVE_1( log2 )
MATHSLIB_PRIMITIVE_1( logb )
MATHSLIB_PRIMITIVE_1( lrint )
MATHSLIB_PRIMITIVE_1( lround )
MATHSLIB_PRIMITIVE_1( nearbyint )
MATHSLIB_PRIMITIVE_1( rint )
MATHSLIB_PRIMITIVE_1( round )
MATHSLIB_PRIMITIVE_1( sinh )
MATHSLIB_PRIMITIVE_1( sqrt )
MATHSLIB_PRIMITIVE_1( tanh )
MATHSLIB_PRIMITIVE_1( tgamma )
MATHSLIB_PRIMITIVE_1( trunc )

#define MATHSLIB_PRIMITIVE_2( NAME ) \
struct item  primitive_##NAME( struct interp *interp, char **p ){ \
 struct item  a = getNumber( interp, p );  if( a.type == ERROR )  return a; \
 struct item  b = getNumber( interp, p );  if( b.type == ERROR )  return b; \
 a.data.number = NAME ( a.data.number, b.data.number );  return a; \
} 

MATHSLIB_PRIMITIVE_2( fdim  )
MATHSLIB_PRIMITIVE_2( atan2 )
MATHSLIB_PRIMITIVE_2( copysign )
MATHSLIB_PRIMITIVE_2( fmax )
MATHSLIB_PRIMITIVE_2( fmin )
MATHSLIB_PRIMITIVE_2( fmod )
MATHSLIB_PRIMITIVE_2( hypot )
MATHSLIB_PRIMITIVE_2( ldexp )
MATHSLIB_PRIMITIVE_2( nextafter )
MATHSLIB_PRIMITIVE_2( nexttoward )
MATHSLIB_PRIMITIVE_2( pow )
MATHSLIB_PRIMITIVE_2( remainder )
MATHSLIB_PRIMITIVE_2( scalbln )
MATHSLIB_PRIMITIVE_2( scalbn )

#define MATHSLIB_PRIMITIVE_3( NAME ) \
struct item  primitive_##NAME( struct interp *interp, char **p ){ \
 struct item  a = getNumber( interp, p );  if( a.type == ERROR )  return a; \
 struct item  b = getNumber( interp, p );  if( b.type == ERROR )  return b; \
 struct item  c = getNumber( interp, p );  if( c.type == ERROR )  return c; \
 a.data.number = NAME ( a.data.number, b.data.number, c.data.number );  return a; \
}

MATHSLIB_PRIMITIVE_3( fma )


struct item  box_MathsLibrary(){
 struct array  *mathLibBox = calloc( 1, sizeof(struct array) );
 const int numFuncs = 53;
 mathLibBox->nDims = 1;
 mathLibBox->dims = calloc( 1, sizeof(unsigned int) );
 mathLibBox->dims[0] = numFuncs * 2;
 mathLibBox->size = numFuncs * 2;
 mathLibBox->itemArray = calloc( numFuncs*2, sizeof(struct item) );
 mathLibBox->arrayContainsMemoryResources = RESOURCE;
 struct item   returnValue = (struct item){0};  returnValue.type = ARRAY;  returnValue.data.array = mathLibBox;
 #define MF( BLAH ) \
  #BLAH , primitive_##BLAH
 void **ptrs = (void*[]){
  MF( ceil ),
  MF( floor ),
  MF( round ),
  MF( sin ),
  MF( cos ),
  MF( atan2 ),
  MF( sqrt ),
  MF( asin ),
  MF( acos ),
  MF( tan ),
  MF( atan ),
  MF( asinh ),
  MF( acosh ),
  MF( atanh ),
  MF( cbrt ),
  MF( cosh ),
  MF( exp ),
  MF( exp2 ),
  MF( expm1 ),
  MF( erf ),
  MF( erfc ),
  MF( fabs ),
  MF( ilogb ),
  MF( lgamma ),
  MF( llrint ),
  MF( llround ),
  MF( log ),
  MF( log10 ),
  MF( log1p ),
  MF( log2 ),
  MF( logb ),
  MF( lrint ),
  MF( lround ),
  MF( nearbyint ),
  MF( rint ),
  MF( sinh ),
  MF( tanh ),
  MF( tgamma ),
  MF( trunc ),
  MF( fdim ),
  MF( copysign ),
  MF( fmax ),
  MF( fmin ),
  MF( fmod ),
  MF( hypot ),
  MF( ldexp ),
  MF( nextafter ),
  MF( nexttoward ),
  MF( pow ),
  MF( remainder ),
  MF( scalbln ),
  MF( scalbn ),
  MF( fma ),
  NULL, NULL
 };
 void *name = (void*)1, *fn;
 for(  int i = 0;  ptrs[i];  i+=2  ){
  name = ptrs[i];  fn = ptrs[i+1];
  // key
  mathLibBox->itemArray[i].type = STRING;  mathLibBox->itemArray[i].data.string = charPtrToNewString( (char*)name, strlen((char*)name) );
  // value
  struct func *func = calloc( 1, sizeof(struct func) );  func->primitive = fn;  func->refCount = 1;
  mathLibBox->itemArray[i+1].type = FUNCTION;  mathLibBox->itemArray[i+1].data.func = func;
 }
 return returnValue;
}