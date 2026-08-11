#pragma once

#include <muparserx/mpParser.h>
#include <muparserx/mpDefines.h>
#include <complex>
#include <string>

namespace con {

class complex_function {
    mup::ParserX parser;
    mup::Value z_var;

public:
    complex_function(const std::string &func_str) {
        parser.DefineVar("z", mup::Variable(&z_var));
        parser.SetExpr(func_str);
    }

    ~complex_function() = default;

    std::complex<double> operator()(std::complex<double> z) {
        z_var = mup::Value{z};
        mup::Value result = parser.Eval();
        return result.GetComplex();
    }
};

}
