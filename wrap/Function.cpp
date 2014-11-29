/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation, 
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 * @file Function.ccp
 * @author Frank Dellaert
 * @date Nov 13, 2014
 **/

#include "Function.h"
#include "utilities.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <iostream>
#include <fstream>

using namespace std;
using namespace wrap;

/* ************************************************************************* */
bool Function::initializeOrCheck(const string& name, const Qualified& instName,
    bool verbose) {

  if (name.empty())
    throw runtime_error("Function::initializeOrCheck called with empty name");

  // Check if this overload is give to the correct method
  if (name_.empty()) {
    name_ = name;
    templateArgValue_ = instName;
    verbose_ = verbose;
    return true;
  } else {
    if (name_ != name || templateArgValue_ != instName || verbose_ != verbose)
      throw runtime_error(
          "Function::initializeOrCheck called with different arguments:  with name "
              + name + " instead of expected " + name_
              + ", or with template argument " + instName.qualifiedName(":")
              + " instead of expected " + templateArgValue_.qualifiedName(":"));

    return false;
  }
}

/* ************************************************************************* */
void Function::emit_call(FileWriter& proxyFile, const ReturnValue& returnVal,
    const string& wrapperName, int id, bool staticMethod) const {
  returnVal.emit_matlab(proxyFile);
  proxyFile.oss << wrapperName << "(" << id;
  if (!staticMethod)
    proxyFile.oss << ", this";
  proxyFile.oss << ", varargin{:});\n";
}

/* ************************************************************************* */
void Function::emit_conditional_call(FileWriter& proxyFile,
    const ReturnValue& returnVal, const ArgumentList& args,
    const string& wrapperName, int id, bool staticMethod) const {

  // Check all arguments
  args.proxy_check_arguments(proxyFile);

  // output call to C++ wrapper
  proxyFile.oss << "        ";
  emit_call(proxyFile, returnVal, wrapperName, id, staticMethod);
}

/* ************************************************************************* */
