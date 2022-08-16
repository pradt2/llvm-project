set(LLVM_LINK_COMPONENTS
        Support
        )

add_clang_library(clangCompressionFrontend
        CompressionASTConsumer.cpp

        LINK_LIBS
        clangAST
        clangBasic
        clangEdit
        clangLex
        clangRewrite
        clangSerialization

        DEPENDS
        omp_gen
        )
