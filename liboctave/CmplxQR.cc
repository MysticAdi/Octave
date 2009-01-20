/*

Copyright (C) 1994, 1995, 1996, 1997, 2002, 2003, 2004, 2005, 2007
              John W. Eaton
Copyright (C) 2008, 2009 Jaroslav Hajek

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CmplxQR.h"
#include "f77-fcn.h"
#include "lo-error.h"
#include "Range.h"
#include "idx-vector.h"
#include "oct-locbuf.h"

extern "C"
{
  F77_RET_T
  F77_FUNC (zgeqrf, ZGEQRF) (const octave_idx_type&, const octave_idx_type&, Complex*,
			     const octave_idx_type&, Complex*, Complex*,
			     const octave_idx_type&, octave_idx_type&); 

  F77_RET_T
  F77_FUNC (zungqr, ZUNGQR) (const octave_idx_type&, const octave_idx_type&, const octave_idx_type&,
			     Complex*, const octave_idx_type&, Complex*,
			     Complex*, const octave_idx_type&, octave_idx_type&);

#ifdef HAVE_QRUPDATE

  F77_RET_T
  F77_FUNC (zqr1up, ZQR1UP) (const octave_idx_type&, const octave_idx_type&, const octave_idx_type&, 
                             Complex*, const octave_idx_type&, Complex*, const octave_idx_type&,
                             Complex*, Complex*, Complex*, double*);

  F77_RET_T
  F77_FUNC (zqrinc, ZQRINC) (const octave_idx_type&, const octave_idx_type&, const octave_idx_type&, 
                             Complex*, const octave_idx_type&, Complex*, const octave_idx_type&,
                             const octave_idx_type&, const Complex*, double*);

  F77_RET_T
  F77_FUNC (zqrdec, ZQRDEC) (const octave_idx_type&, const octave_idx_type&, const octave_idx_type&, 
                             Complex*, const octave_idx_type&, Complex*, const octave_idx_type&,
                             const octave_idx_type&, double*);

  F77_RET_T
  F77_FUNC (zqrinr, ZQRINR) (const octave_idx_type&, const octave_idx_type&, 
                             Complex*, const octave_idx_type&, Complex*, const octave_idx_type&,
                             const octave_idx_type&, const Complex*, double*);

  F77_RET_T
  F77_FUNC (zqrder, ZQRDER) (const octave_idx_type&, const octave_idx_type&, 
                             Complex*, const octave_idx_type&, Complex*, const octave_idx_type&,
                             const octave_idx_type&, Complex*, double*);

  F77_RET_T
  F77_FUNC (zqrshc, ZQRSHC) (const octave_idx_type&, const octave_idx_type&, const octave_idx_type&,
                             Complex*, const octave_idx_type&, Complex*, const octave_idx_type&,
                             const octave_idx_type&, const octave_idx_type&,
                             Complex*, double*);

#endif
}

ComplexQR::ComplexQR (const ComplexMatrix& a, QR::type qr_type)
  : q (), r ()
{
  init (a, qr_type);
}

void
ComplexQR::init (const ComplexMatrix& a, QR::type qr_type)
{
  octave_idx_type m = a.rows ();
  octave_idx_type n = a.cols ();

  if (m == 0 || n == 0)
    {
      (*current_liboctave_error_handler)
	("ComplexQR must have non-empty matrix");
      return;
    }

  octave_idx_type min_mn = m < n ? m : n;

  Array<Complex> tau (min_mn);
  Complex *ptau = tau.fortran_vec ();

  octave_idx_type lwork = 32*n;
  Array<Complex> work (lwork);
  Complex *pwork = work.fortran_vec ();

  octave_idx_type info = 0;

  ComplexMatrix A_fact;
  if (m > n && qr_type != QR::economy)
    {
      A_fact.resize (m, m);
      A_fact.insert (a, 0, 0);
    }
  else
    A_fact = a;

  Complex *tmp_data = A_fact.fortran_vec ();

  F77_XFCN (zgeqrf, ZGEQRF, (m, n, tmp_data, m, ptau, pwork, lwork, info));

  if (qr_type == QR::raw)
    {
      for (octave_idx_type j = 0; j < min_mn; j++)
	{
	  octave_idx_type limit = j < min_mn - 1 ? j : min_mn - 1;
	  for (octave_idx_type i = limit + 1; i < m; i++)
	    A_fact.elem (i, j) *= tau.elem (j);
	}

      r = A_fact;

      if (m > n)
	r.resize (m, n);
    }
  else
    {
      octave_idx_type n2 = (qr_type == QR::economy) ? min_mn : m;

      if (qr_type == QR::economy && m > n)
	r.resize (n, n, 0.0);
      else
	r.resize (m, n, 0.0);

      for (octave_idx_type j = 0; j < n; j++)
	{
	  octave_idx_type limit = j < min_mn-1 ? j : min_mn-1;
	  for (octave_idx_type i = 0; i <= limit; i++)
	    r.elem (i, j) = A_fact.elem (i, j);
	}

      lwork = 32 * n2;
      work.resize (lwork);
      Complex *pwork2 = work.fortran_vec ();

      F77_XFCN (zungqr, ZUNGQR, (m, n2, min_mn, tmp_data, m, ptau,
				 pwork2, lwork, info));

      q = A_fact;
      q.resize (m, n2);
    }
}

ComplexQR::ComplexQR (const ComplexMatrix& q_arg, const ComplexMatrix& r_arg)
{
  if (q_arg.columns () != r_arg.rows ()) 
    {
      (*current_liboctave_error_handler) ("QR dimensions mismatch");
      return;
    }

  this->q = q_arg;
  this->r = r_arg;
}

#ifdef HAVE_QRUPDATE

void
ComplexQR::update (const ComplexColumnVector& u, const ComplexColumnVector& v)
{
  octave_idx_type m = q.rows ();
  octave_idx_type n = r.columns ();
  octave_idx_type k = q.columns ();

  if (u.length () == m && v.length () == n)
    {
      ComplexColumnVector utmp = u, vtmp = v;
      OCTAVE_LOCAL_BUFFER (Complex, w, k);
      OCTAVE_LOCAL_BUFFER (double, rw, k);
      F77_XFCN (zqr1up, ZQR1UP, (m, n, k, q.fortran_vec (), m, r.fortran_vec (), k,
                                 utmp.fortran_vec (), vtmp.fortran_vec (), w, rw));
    }
  else
    (*current_liboctave_error_handler) ("QR update dimensions mismatch");
}

void
ComplexQR::update (const ComplexMatrix& u, const ComplexMatrix& v)
{
  octave_idx_type m = q.rows ();
  octave_idx_type n = r.columns ();
  octave_idx_type k = q.columns ();

  if (u.rows () == m && v.rows () == n && u.cols () == v.cols ())
    {
      OCTAVE_LOCAL_BUFFER (Complex, w, k);
      OCTAVE_LOCAL_BUFFER (double, rw, k);
      for (octave_idx_type i = 0; i < u.cols (); i++)
        {
          ComplexColumnVector utmp = u.column (i), vtmp = v.column (i);
          F77_XFCN (zqr1up, ZQR1UP, (m, n, k, q.fortran_vec (), m, r.fortran_vec (), k,
                                     utmp.fortran_vec (), vtmp.fortran_vec (), w, rw));
        }
    }
  else
    (*current_liboctave_error_handler) ("qrupdate: dimensions mismatch");
}

void
ComplexQR::insert_col (const ComplexColumnVector& u, octave_idx_type j)
{
  octave_idx_type m = q.rows ();
  octave_idx_type n = r.columns ();
  octave_idx_type k = q.columns ();

  if (u.length () != m)
    (*current_liboctave_error_handler) ("qrinsert: dimensions mismatch");
  else if (j < 0 || j > n) 
    (*current_liboctave_error_handler) ("qrinsert: index out of range");
  else
    {
      if (k < m)
        {
          q.resize (m, k+1);
          r.resize (k+1, n+1);
        }
      else
        {
          r.resize (k, n+1);
        }

      ComplexColumnVector utmp = u;
      OCTAVE_LOCAL_BUFFER (double, rw, k);
      F77_XFCN (zqrinc, ZQRINC, (m, n, k, q.fortran_vec (), q.rows (),
                                 r.fortran_vec (), r.rows (), j + 1, 
                                 utmp.data (), rw));
    }
}

void
ComplexQR::insert_col (const ComplexMatrix& u, const Array<octave_idx_type>& j)
{
  octave_idx_type m = q.rows ();
  octave_idx_type n = r.columns ();
  octave_idx_type k = q.columns ();

  Array<octave_idx_type> jsi;
  Array<octave_idx_type> js = j.sort (jsi, ASCENDING);
  octave_idx_type nj = js.length ();
  bool dups = false;
  for (octave_idx_type i = 0; i < nj - 1; i++)
    dups = dups && js(i) == js(i+1);

  if (dups)
    (*current_liboctave_error_handler) ("qrinsert: duplicate index detected");
  else if (u.length () != m || u.columns () != nj)
    (*current_liboctave_error_handler) ("qrinsert: dimensions mismatch");
  else if (nj > 0 && (js(0) < 0 || js(nj-1) > n))
    (*current_liboctave_error_handler) ("qrinsert: index out of range");
  else if (nj > 0)
    {
      octave_idx_type kmax = std::min (k + nj, m);
      if (k < m)
        {
          q.resize (m, kmax);
          r.resize (kmax, n + nj);
        }
      else
        {
          r.resize (k, n + nj);
        }

      OCTAVE_LOCAL_BUFFER (double, rw, kmax);
      for (octave_idx_type i = 0; i < js.length (); i++)
        {
          ComplexColumnVector utmp = u.column (jsi(i));
          F77_XFCN (zqrinc, ZQRINC, (m, n + i, std::min (kmax, k + i), 
                                     q.fortran_vec (), q.rows (),
                                     r.fortran_vec (), r.rows (), js(i) + 1, 
                                     utmp.data (), rw));
        }
    }
}

void
ComplexQR::delete_col (octave_idx_type j)
{
  octave_idx_type m = q.rows ();
  octave_idx_type k = r.rows ();
  octave_idx_type n = r.columns ();

  if (j < 0 || j > n-1) 
    (*current_liboctave_error_handler) ("qrdelete: index out of range");
  else
    {
      OCTAVE_LOCAL_BUFFER (double, rw, k);
      F77_XFCN (zqrdec, ZQRDEC, (m, n, k, q.fortran_vec (), q.rows (),
				 r.fortran_vec (), r.rows (), j + 1, rw));

      if (k < m)
        {
          q.resize (m, k-1);
          r.resize (k-1, n-1);
        }
      else
        {
          r.resize (k, n-1);
        }
    }
}

void
ComplexQR::delete_col (const Array<octave_idx_type>& j)
{
  octave_idx_type m = q.rows ();
  octave_idx_type n = r.columns ();
  octave_idx_type k = q.columns ();

  Array<octave_idx_type> jsi;
  Array<octave_idx_type> js = j.sort (jsi, DESCENDING);
  octave_idx_type nj = js.length ();
  bool dups = false;
  for (octave_idx_type i = 0; i < nj - 1; i++)
    dups = dups && js(i) == js(i+1);

  if (dups)
    (*current_liboctave_error_handler) ("qrinsert: duplicate index detected");
  else if (nj > 0 && (js(0) > n-1 || js(nj-1) < 0))
    (*current_liboctave_error_handler) ("qrinsert: index out of range");
  else if (nj > 0)
    {
      OCTAVE_LOCAL_BUFFER (double, rw, k);
      for (octave_idx_type i = 0; i < js.length (); i++)
        {
          F77_XFCN (zqrdec, ZQRDEC, (m, n - i, k == m ? k : k - i, 
                                     q.fortran_vec (), q.rows (),
                                     r.fortran_vec (), r.rows (), js(i) + 1, rw));
        }
      if (k < m)
        {
          q.resize (m, k - nj);
          r.resize (k - nj, n - nj);
        }
      else
        {
          r.resize (k, n - nj);
        }

    }
}

void
ComplexQR::insert_row (const ComplexRowVector& u, octave_idx_type j)
{
  octave_idx_type m = r.rows ();
  octave_idx_type n = r.columns ();
  octave_idx_type k = std::min (m, n);

  if (! q.is_square () || u.length () != n)
    (*current_liboctave_error_handler) ("qrinsert: dimensions mismatch");
  else if (j < 0 || j > m) 
    (*current_liboctave_error_handler) ("qrinsert: index out of range");
  else
    {
      q.resize (m + 1, m + 1);
      r.resize (m + 1, n);
      ComplexRowVector utmp = u;
      OCTAVE_LOCAL_BUFFER (double, rw, k);
      F77_XFCN (zqrinr, ZQRINR, (m, n, q.fortran_vec (), q.rows (),
				 r.fortran_vec (), r.rows (), 
                                 j + 1, utmp.fortran_vec (), rw));

    }
}

void
ComplexQR::delete_row (octave_idx_type j)
{
  octave_idx_type m = r.rows ();
  octave_idx_type n = r.columns ();

  if (! q.is_square ())
    (*current_liboctave_error_handler) ("qrdelete: dimensions mismatch");
  else if (j < 0 || j > m-1) 
    (*current_liboctave_error_handler) ("qrdelete: index out of range");
  else
    {
      OCTAVE_LOCAL_BUFFER (Complex, w, m);
      OCTAVE_LOCAL_BUFFER (double, rw, m);
      F77_XFCN (zqrder, ZQRDER, (m, n, q.fortran_vec (), q.rows (),
				 r.fortran_vec (), r.rows (), j + 1,
                                 w, rw));

      q.resize (m - 1, m - 1);
      r.resize (m - 1, n);
    }
}

void
ComplexQR::shift_cols (octave_idx_type i, octave_idx_type j)
{
  octave_idx_type m = q.rows ();
  octave_idx_type k = r.rows ();
  octave_idx_type n = r.columns ();

  if (i < 0 || i > n-1 || j < 0 || j > n-1) 
    (*current_liboctave_error_handler) ("qrshift: index out of range");
  else
    {
      OCTAVE_LOCAL_BUFFER (Complex, w, k);
      OCTAVE_LOCAL_BUFFER (double, rw, k);
      F77_XFCN (zqrshc, ZQRSHC, (m, n, k, 
                                 q.fortran_vec (), q.rows (),
                                 r.fortran_vec (), r.rows (),
                                 i + 1, j + 1, w, rw));
    }
}

#endif

/*
;;; Local Variables: ***
;;; mode: C++ ***
;;; End: ***
*/
