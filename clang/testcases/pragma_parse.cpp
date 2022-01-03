#pragma dastgen annotate myGlobalFunc
void myGlobalFunc() {}

#pragma dastgen annotate MyStruct
struct
    MyStruct {

  #pragma dastgen annotate myBool
  #pragma dastgen annotate myBool2
  bool myBool;

  #pragma dastgen annotate myInt1 myInt2
  int myInt1, myInt2;

  #pragma dastgen annotate myMethod
  void myMethod() {}

  void myMethodWithArgs(
                        #pragma dastgen annotate arg0
                        int arg0) {}

  int myMethodWithStatements() {
//    #pragma dastgen annotate statement
    return 1;
  }

};

#pragma dastgen annotate MyMultiPragmaStruct ONE
#pragma dastgen annotate MyMiltiPragmaStruct TWO
struct MyMultiPragmaStruct {};