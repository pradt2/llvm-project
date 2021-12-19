struct PackedData {bool packedA, packedB, pc;};
int main(int argc, char** argv) {
  PackedData packedDataObj;

  packedDataObj.packedA = 12 >> 1;
//  packedDataObj.packedA = packedDataObj.packedA || false;

  return sizeof packedDataObj;
//  return packedDataObj.packedA;
}
