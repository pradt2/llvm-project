//TranslationUnitDecl 0x17c5208 <<invalid sloc>> <invalid sloc>
//`-VarDecl 0x180b288 <./testcases/01_global_var_decl.cpp:4:1, col:5> col:5 packedDataObj 'int'

#include "00_replaceable_types.h"

ReplaceableSimpleStruct packedDataObj;
ReplaceableSimpleStruct *packedDataObjPtr;
ReplaceableSimpleStruct **packedDataObjPtr2;
ReplaceableSimpleStruct &&packedDataObjRef2 = {};