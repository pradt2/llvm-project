#include "ILayoutOptimiser.h"

class MemoryLayoutOptimiserResolver {
private:
    const std::string accessOpt = "opt_access";
    const std::string packingOpt = "opt_packing";
    const std::string packingOptBudget = "opt_packing_budget";

    ILayoutOptimiser *defaultOpt = new NopLayoutOptimiser();

public:
    ILayoutOptimiser *resolve(Annotation *compressedDeclAnnotation) {
//        if (compressedDeclAnnotation->tokens.size() == 1) return this->defaultOpt;
//        else if (compressedDeclAnnotation->tokens[1] == accessOpt) return new AccessTimeLayoutOptimiser();
//        else if (compressedDeclAnnotation->tokens[1] == packingOpt) return new MemoryEfficiencyLayoutOptimiser();
//        else if (compressedDeclAnnotation->tokens[1] == packingOptBudget) {
//            int budget = std::stoi(compressedDeclAnnotation->tokens[2]);
//            return new MemoryEfficiencyLayoutOptimiser(budget);
//        }
//        std::cerr << "Unable to resolve the memory layout optimiser" << std::endl;
//        return nullptr;
          return this->defaultOpt;
    }
};
