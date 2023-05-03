#pragma once
///////////////////////////////////////////////////////////////////////////////
// File: warnings.h
///////////////////////////////////////////////////////////////////////////////
//
// The code in this file is released under the The MIT License (MIT)
//
// Copyright (c) 2023 JetByte Limited.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the “Software”), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
///////////////////////////////////////////////////////////////////////////////

#pragma warning (disable: 4061)  // enumerator 'x' in switch of enum 'y' is not explicitly handled by a case label
#pragma warning (disable: 4365)  // 'initializing': conversion from 'int' to 'size_t', signed/unsigned mismatch
#pragma warning (disable: 4514)  // 'x' unreferenced inline function has been removed
#pragma warning (disable: 4668)  // 'x' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif'
#pragma warning (disable: 4710)  // 'x': function not inlined
#pragma warning (disable: 4738)  // storing 32-bit float result in memory, possible loss of performance
#pragma warning (disable: 4820)  // 'x': 'y' bytes padding added after data member 'z'
#pragma warning (disable: 4623)  // 'x': default constructor was implicitly defined as deleted
#pragma warning (disable: 4625)  // 'x': copy constructor was implicitly defined as deleted
#pragma warning (disable: 4626)  // 'x': assignment operator was implicitly defined as deleted
#pragma warning (disable: 5026)  // 'x': move constructor was implicitly defined as deleted
#pragma warning (disable: 5027)  // 'x': move assignment operator was implicitly defined as deleted
#pragma warning (disable: 5045)  // Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified

///////////////////////////////////////////////////////////////////////////////
// End of file: warnings.h
///////////////////////////////////////////////////////////////////////////////
