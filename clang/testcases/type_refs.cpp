class PackedData { public: bool bool_a; PackedData getPackedData() {};};

class PackedDataSubclass : public PackedData {};

template<typename __PackedType>
class PackedDataHolder {
public:
  __PackedType packedDataInnerObj;
};


PackedData p;

PackedData func1(PackedData packedDataObj, PackedData &packedDataRef, PackedData *packedDataPtr, PackedData **packedDataPtrDouble) {
  packedDataRef.bool_a = true;
  packedDataPtr->bool_a = true;
  (*packedDataPtrDouble)->bool_a = true;
  PackedData packedDataLocalVar;
  packedDataLocalVar.bool_a = true;
  PackedDataHolder<PackedData> packedDataHolder;
  packedDataHolder.packedDataInnerObj = packedDataObj;
  PackedData *castPtr = reinterpret_cast<PackedData*>(0);
  PackedData *castPtr2 = dynamic_cast<PackedData*>(packedDataPtr);
  PackedData *castPtr3 = static_cast<PackedData*>(packedDataPtr);
  PackedData packedData = (PackedData) packedDataObj;
  PackedData *newPackedData = new PackedData();
  PackedData packedDataArr[2];
  return func1(packedDataObj, packedDataRef, packedDataPtr, packedDataPtrDouble);
}