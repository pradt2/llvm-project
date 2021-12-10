struct PackedData {bool packedA, packedB, pc;};
int main() {
  PackedData packedDataObj;
//  PackedData packedDataObj2;
//  bool a = false;
//  bool b = false;
  packedDataObj.packedA = true;
//  bool *ap = &a;
//  bool *bp = &b;

//  packedDataObj.packedB = true;
//  if (!packedDataObj.packedA) return -1;
//  if (!packedDataObj.packedB) return -2;
//  PackedData *p1 = &packedDataObj;
//  PackedData *p2 = &packedDataObj2;

//  return (p1 - p2);
//  return (ap - bp);
//  return sizeof (PackedData);
//  return sizeof packedDataObj;
  return packedDataObj.pc;
}
