/*

Copyright (C) 2006, 2007 John W. Eaton

This file is part of Octave.

Octave is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

Octave is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with Octave; see the file COPYING.  If not, see
<http://www.gnu.org/licenses/>.

*/

#include "mex.h"

void
mexFunction (int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[])
{
  char *str;
  mxArray *v;

  if (nrhs != 2 || ! mxIsString (prhs[0]))
    mexErrMsgTxt ("expects symbol name and value");

  str = mxArrayToString (prhs[0]);

  v = mexGetArray (str, "global");

  if (v)
    {
      mexPrintf ("%s is a global variable with the following value:\n", str);
      mexCallMATLAB (0, 0, 1, &v, "disp");
    }

  v = mexGetArray (str, "caller");

  if (v)
    {
      mexPrintf ("%s is a caller variable with the following value:\n", str);
      mexCallMATLAB (0, 0, 1, &v, "disp");
    }

  // WARNING!! Can't do this in MATLAB!  Must copy variable first.
  mxSetName (prhs[1], str);  
  mexPutArray (prhs[1], "caller");
}
