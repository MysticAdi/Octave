/*

Copyright (C) 2009-2017 Shai Ayal

This file is part of Octave.

Octave is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Octave is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Octave; see the file COPYING.  If not, see
<http://www.gnu.org/licenses/>.

*/

#if defined (HAVE_CONFIG_H)
#  include "config.h"
#endif

// Both header files are required outside of HAVE_GLP2S_H
#include "errwarn.h"
#include "gl2ps-print.h"

#if defined (HAVE_GL2PS_H) && defined (HAVE_OPENGL)

#include <cstdio>

#include <limits>

#include <gl2ps.h>

#include "lo-mappers.h"
#include "oct-locbuf.h"
#include "tmpfile-wrapper.h"
#include "unistd-wrappers.h"
#include "unwind-prot.h"

#include "gl-render.h"
#include "oct-opengl.h"
#include "sighandlers.h"
#include "sysdep.h"
#include "text-renderer.h"

namespace octave
{
  class
  OCTINTERP_API
  gl2ps_renderer : public opengl_renderer
  {
  public:

    gl2ps_renderer (FILE *_fp, const std::string& _term)
      : octave::opengl_renderer () , fp (_fp), term (_term), fontsize (),
        fontname (), buffer_overflow (false)
    { }

    ~gl2ps_renderer (void) = default;

    // FIXME: should we import the functions from the base class and
    // overload them here, or should we use a different name so we don't
    // have to do this?  Without the using declaration or a name change,
    // the base class functions will be hidden.  That may be OK, but it
    // can also cause some confusion.
    using octave::opengl_renderer::draw;

    void draw (const graphics_object& go, const std::string& print_cmd);

  protected:

    Matrix render_text (const std::string& txt,
                        double x, double y, double z,
                        int halign, int valign, double rotation = 0.0);

    void set_font (const base_properties& props);

    void draw_axes (const axes::properties& props)
    {
      // Initialize a sorting tree (viewport) in gl2ps for each axes
      GLint vp[4];
      glGetIntegerv (GL_VIEWPORT, vp);
      gl2psBeginViewport (vp);

      // Draw and finish () or there may primitives missing in the gl2ps output.
      octave::opengl_renderer::draw_axes (props);
      finish ();

      // Finalize viewport
      GLint state = gl2psEndViewport ();
      if (state == GL2PS_NO_FEEDBACK)
        warning ("gl2ps_renderer::draw_axes: empty feedback buffer and/or nothing else to print");
      else if (state == GL2PS_ERROR)
        error ("gl2ps_renderer::draw_axes: gl2psEndPage returned GL2PS_ERROR");

      buffer_overflow |= (state == GL2PS_OVERFLOW);

      // Don't draw background for subsequent viewports (legends, subplots, etc.)
      GLint opts;
      gl2psGetOptions (&opts);
      opts &= ~GL2PS_DRAW_BACKGROUND;
      gl2psSetOptions (opts);
    }

    void draw_text (const text::properties& props);

    void draw_pixels (int w, int h, const float *data);
    void draw_pixels (int w, int h, const uint8_t *data);
    void draw_pixels (int w, int h, const uint16_t *data);

    void set_linestyle (const std::string& s, bool use_stipple = false,
                        double linewidth = 0.5)
    {
      octave::opengl_renderer::set_linestyle (s, use_stipple, linewidth);

      if (s == "-" && ! use_stipple)
        gl2psDisable (GL2PS_LINE_STIPPLE);
      else
        gl2psEnable (GL2PS_LINE_STIPPLE);
    }

    void set_linecap (const std::string& s)
      {
        octave::opengl_renderer::set_linejoin (s);

#if defined (HAVE_GL2PSLINEJOIN)
        if (s == "butt")
          gl2psLineCap (GL2PS_LINE_CAP_BUTT);
        else if (s == "square")
          gl2psLineCap (GL2PS_LINE_CAP_SQUARE);
        else if (s == "round")
          gl2psLineCap (GL2PS_LINE_CAP_ROUND);
#endif
      }

    void set_linejoin (const std::string& s)
    {
      octave::opengl_renderer::set_linejoin (s);

#if defined (HAVE_GL2PSLINEJOIN)
      if (s == "round")
        gl2psLineJoin (GL2PS_LINE_JOIN_ROUND);
      else if (s == "miter")
        gl2psLineJoin (GL2PS_LINE_JOIN_MITER);
      else if (s == "chamfer")
        gl2psLineJoin (GL2PS_LINE_JOIN_BEVEL);
#endif
    }

    void set_polygon_offset (bool on, float offset = 0.0f)
    {
      if (on)
        {
          octave::opengl_renderer::set_polygon_offset (on, offset);
          gl2psEnable (GL2PS_POLYGON_OFFSET_FILL);
        }
      else
        {
          gl2psDisable (GL2PS_POLYGON_OFFSET_FILL);
          octave::opengl_renderer::set_polygon_offset (on, offset);
        }
    }

    void set_linewidth (float w)
    {
      gl2psLineWidth (w);
    }

  private:

    // Use xform to compute the coordinates of the string list
    // that have been parsed by freetype
    void fix_strlist_position (double x, double y, double z,
                               Matrix box, double rotation,
                               std::list<octave::text_renderer::string>& lst);

    int alignment_to_mode (int ha, int va) const;
    FILE *fp;
    caseless_str term;
    double fontsize;
    std::string fontname;
    bool buffer_overflow;
  };

  void
  gl2ps_renderer::draw (const graphics_object& go, const std::string& print_cmd)
  {
    static bool in_draw = false;
    static std::string old_print_cmd;
    static GLint buffsize;

    if (! in_draw)
      {
        octave::unwind_protect frame;

        frame.protect_var (in_draw);

        in_draw = true;

        GLint gl2ps_term = GL2PS_PS;
        if (term.find ("eps") != std::string::npos)
          gl2ps_term = GL2PS_EPS;
        else if (term.find ("pdf") != std::string::npos)
          gl2ps_term = GL2PS_PDF;
        else if (term.find ("ps") != std::string::npos)
          gl2ps_term = GL2PS_PS;
        else if (term.find ("svg") != std::string::npos)
          gl2ps_term = GL2PS_SVG;
        else if (term.find ("pgf") != std::string::npos)
          gl2ps_term = GL2PS_PGF;
        else if (term.find ("tex") != std::string::npos)
          gl2ps_term = GL2PS_TEX;
        else
          warning ("gl2ps_renderer::draw: Unknown terminal %s, using 'ps'",
                   term.c_str ());

        GLint gl2ps_text = 0;
        if (term.find ("notxt") != std::string::npos)
          gl2ps_text = GL2PS_NO_TEXT;

        // Default sort order optimizes for 3D plots
        GLint gl2ps_sort = GL2PS_BSP_SORT;

        // For 2D plots we can use a simpler Z-depth sorting algorithm
        // FIXME: gl2ps does not provide a way to change the sorting algorythm
        // on a viewport basis, we thus disable sorting only if all axes are 2D
        if (term.find ("is2D") != std::string::npos)
          gl2ps_sort = GL2PS_NO_SORT;

        // Use a temporary file in case an overflow happens
        FILE *tmpf = octave_tmpfile_wrapper ();

        if (! tmpf)
          error ("gl2ps_renderer::draw: couldn't open temporary file for printing");

        // Reset buffsize, unless this is 2nd pass of a texstandalone print.
        if (term.find ("tex") == std::string::npos)
          buffsize = 2*1024*1024;
        else
          buffsize /= 2;

        buffer_overflow = true;

        while (buffer_overflow)
          {
            buffer_overflow = false;
            buffsize *= 2;

            std::fseek (tmpf, 0, SEEK_SET);
            octave_ftruncate_wrapper (fileno (tmpf), 0);

            // For LaTeX output the print process uses 2 drawnow() commands.
            // The first one is for the pdf/ps/eps graph to be included.  The
            // print_cmd is saved as old_print_cmd.  Then the second drawnow()
            // outputs the tex-file and the graphic filename to be included is
            // extracted from old_print_cmd.

            std::string include_graph;

            size_t found_redirect = old_print_cmd.find (">");

            if (found_redirect != std::string::npos)
              include_graph = old_print_cmd.substr (found_redirect + 1);
            else
              include_graph = old_print_cmd;

            size_t n_begin = include_graph.find_first_not_of (" ");

            if (n_begin != std::string::npos)
              {
                size_t n_end = include_graph.find_last_not_of (" ");
                include_graph = include_graph.substr (n_begin,
                                                      n_end - n_begin + 1);
              }
            else
              include_graph = "foobar-inc";

            // GL2PS_SILENT was removed to allow gl2ps to print errors on stderr
            GLint ret = gl2psBeginPage ("gl2ps_renderer figure", "Octave", 0,
                                        gl2ps_term, gl2ps_sort,
                                        (GL2PS_NO_BLENDING
                                         | GL2PS_OCCLUSION_CULL
                                         | GL2PS_BEST_ROOT
                                         | gl2ps_text
                                         | GL2PS_DRAW_BACKGROUND
                                         | GL2PS_NO_PS3_SHADING
                                         | GL2PS_USE_CURRENT_VIEWPORT),
                                        GL_RGBA, 0, 0, 0, 0, 0,
                                        buffsize, tmpf, include_graph.c_str ());
            if (ret == GL2PS_ERROR)
              {
                old_print_cmd.clear ();
                error ("gl2ps_renderer::draw: gl2psBeginPage returned GL2PS_ERROR");
              }

            octave::opengl_renderer::draw (go);

            if (buffer_overflow)
              warning ("gl2ps_renderer::draw: retrying with buffer size: %.1E B\n", double (2*buffsize));

            if (! buffer_overflow)
              old_print_cmd = print_cmd;

            // Don't check return value of gl2psEndPage, it is not meaningful.
            // Errors and warnings are checked after gl2psEndViewport in
            // gl2ps_renderer::draw_axes instead.
            gl2psEndPage ();
          }

        // Copy temporary file to pipe
        std::fseek (tmpf, 0, SEEK_SET);
        char str[8192];  // 8 kB is a common kernel buffersize
        size_t nread, nwrite;
        nread = 1;
        while (! feof (tmpf) && nread)
          {
            nread = std::fread (str, 1, 8192, tmpf);
            if (nread)
              {
                nwrite = std::fwrite (str, 1, nread, fp);
                if (nwrite != nread)
                  {
                    octave::signal_handler ();   // Clear SIGPIPE signal
                    error ("gl2ps_renderer::draw: internal pipe error");
                  }
              }
          }
      }
    else
      octave::opengl_renderer::draw (go);
  }

  int
  gl2ps_renderer::alignment_to_mode (int ha, int va) const
  {
    int gl2psa = GL2PS_TEXT_BL;

    if (ha == 0)
      {
        if (va == 0 || va == 3)
          gl2psa=GL2PS_TEXT_BL;
        else if (va == 2)
          gl2psa=GL2PS_TEXT_TL;
        else if (va == 1)
          gl2psa=GL2PS_TEXT_CL;
      }
    else if (ha == 2)
      {
        if (va == 0 || va == 3)
          gl2psa=GL2PS_TEXT_BR;
        else if (va == 2)
          gl2psa=GL2PS_TEXT_TR;
        else if (va == 1)
          gl2psa=GL2PS_TEXT_CR;
      }
    else if (ha == 1)
      {
        if (va == 0 || va == 3)
          gl2psa=GL2PS_TEXT_B;
        else if (va == 2)
          gl2psa=GL2PS_TEXT_T;
        else if (va == 1)
          gl2psa=GL2PS_TEXT_C;
      }

    return gl2psa;
  }

  void
  gl2ps_renderer::fix_strlist_position (double x, double y, double z,
                                        Matrix box, double rotation,
                                        std::list<octave::text_renderer::string>& lst)
  {
    for (auto& txtobj : lst)
      {
        // Get pixel coordinates
        ColumnVector coord_pix = get_transform ().transform (x, y, z, false);

        // Translate and rotate
        double rot = rotation * 4.0 * atan (1.0) / 180;
        coord_pix(0) += (txtobj.get_x () + box(0))*cos (rot)
                        - (txtobj.get_y () + box(1))*sin (rot);
        coord_pix(1) -= (txtobj.get_y () + box(1))*cos (rot)
                        + (txtobj.get_x () + box(0))*sin (rot);

        // Turn coordinates back into current gl coordinates
        ColumnVector coord = get_transform ().untransform (coord_pix(0),
                                                           coord_pix(1),
                                                           coord_pix(2),
                                                           false);
        txtobj.set_x (coord(0));
        txtobj.set_y (coord(1));
        txtobj.set_z (coord(2));
      }
  }
}

static std::string
code_to_symbol (uint32_t code)
{
  std::string retval;

  uint32_t idx = code - 945;
  if (idx < 25)
    {
      std::string characters("abgdezhqiklmnxoprVstufcyw");
      retval = characters[idx];
      return retval;
    }

  idx = code - 913;
  if (idx < 25)
    {
      std::string characters("ABGDEZHQIKLMNXOPRVSTUFCYW");
      retval = characters[idx];
    }
  else if (code == 978)
    retval = std::string ("U");
  else if (code == 215)
    retval = std::string ("\xb4");
  else if (code == 177)
    retval = std::string ("\xb1");
  else if (code == 8501)
    retval = std::string ("\xc0");
  else if (code == 8465)
    retval = std::string ("\xc1");
  else if (code == 8242)
    retval = std::string ("\xa2");
  else if (code == 8736)
    retval = std::string ("\xd0");
  else if (code == 172)
    retval = std::string ("\xd8");
  else if (code == 9829)
    retval = std::string ("\xa9");
  else if (code == 8472)
    retval = std::string ("\xc3");
  else if (code == 8706)
    retval = std::string ("\xb6");
  else if (code == 8704)
    retval = std::string ("\x22");
  else if (code == 9827)
    retval = std::string ("\xa7");
  else if (code == 9824)
    retval = std::string ("\xaa");
  else if (code == 8476)
    retval = std::string ("\xc2");
  else if (code == 8734)
    retval = std::string ("\xa5");
  else if (code == 8730)
    retval = std::string ("\xd6");
  else if (code == 8707)
    retval = std::string ("\x24");
  else if (code == 9830)
    retval = std::string ("\xa8");
  else if (code == 8747)
    retval = std::string ("\xf2");
  else if (code == 8727)
    retval = std::string ("\x2a");
  else if (code == 8744)
    retval = std::string ("\xda");
  else if (code == 8855)
    retval = std::string ("\xc4");
  else if (code == 8901)
    retval = std::string ("\xd7");
  else if (code == 8728)
    retval = std::string ("\xb0");
  else if (code == 8745)
    retval = std::string ("\xc7");
  else if (code == 8743)
    retval = std::string ("\xd9");
  else if (code == 8856)
    retval = std::string ("\xc6");
  else if (code == 8729)
    retval = std::string ("\xb7");
  else if (code == 8746)
    retval = std::string ("\xc8");
  else if (code == 8853)
    retval = std::string ("\xc5");
  else if (code == 8804)
    retval = std::string ("\xa3");
  else if (code == 8712)
    retval = std::string ("\xce");
  else if (code == 8839)
    retval = std::string ("\xca");
  else if (code == 8801)
    retval = std::string ("\xba");
  else if (code == 8773)
    retval = std::string ("\x40");
  else if (code == 8834)
    retval = std::string ("\xcc");
  else if (code == 8805)
    retval = std::string ("\xb3");
  else if (code == 8715)
    retval = std::string ("\x27");
  else if (code == 8764)
    retval = std::string ("\x7e");
  else if (code == 8733)
    retval = std::string ("\xb5");
  else if (code == 8838)
    retval = std::string ("\xcd");
  else if (code == 8835)
    retval = std::string ("\xc9");
  else if (code == 8739)
    retval = std::string ("\xbd");
  else if (code == 8776)
    retval = std::string ("\xbb");
  else if (code == 8869)
    retval = std::string ("\x5e");
  else if (code == 8656)
    retval = std::string ("\xdc");
  else if (code == 8592)
    retval = std::string ("\xac");
  else if (code == 8658)
    retval = std::string ("\xde");
  else if (code == 8594)
    retval = std::string ("\xae");
  else if (code == 8596)
    retval = std::string ("\xab");
  else if (code == 8593)
    retval = std::string ("\xad");
  else if (code == 8595)
    retval = std::string ("\xaf");
  else if (code == 8970)
    retval = std::string ("\xeb");
  else if (code == 8971)
    retval = std::string ("\xfb");
  else if (code == 10216)
    retval = std::string ("\xe1");
  else if (code == 10217)
    retval = std::string ("\xf1");
  else if (code == 8968)
    retval = std::string ("\xe9");
  else if (code == 8969)
    retval = std::string ("\xf9");
  else if (code == 8800)
    retval = std::string ("\xb9");
  else if (code == 8230)
    retval = std::string ("\xbc");
  else if (code == 176)
    retval = std::string ("\xb0");
  else if (code == 8709)
    retval = std::string ("\xc6");
  else if (code == 169)
    retval = std::string ("\xd3");

  if (retval.empty ())
    warning ("print: unhandled symbol %d", code);

  return retval;
}

static std::string
select_font (caseless_str fn, bool isbold, bool isitalic)
{
  std::transform (fn.begin (), fn.end (), fn.begin (), ::tolower);
  std::string fontname;
  if (fn == "times" || fn == "times-roman")
    {
      if (isitalic && isbold)
        fontname = "Times-BoldItalic";
      else if (isitalic)
        fontname = "Times-Italic";
      else if (isbold)
        fontname = "Times-Bold";
      else
        fontname = "Times-Roman";
    }
  else if (fn == "courier")
    {
      if (isitalic && isbold)
        fontname = "Courier-BoldOblique";
      else if (isitalic)
        fontname = "Courier-Oblique";
      else if (isbold)
        fontname = "Courier-Bold";
      else
        fontname = "Courier";
    }
  else if (fn == "symbol")
    fontname = "Symbol";
  else if (fn == "zapfdingbats")
    fontname = "ZapfDingbats";
  else
    {
      if (isitalic && isbold)
        fontname = "Helvetica-BoldOblique";
      else if (isitalic)
        fontname = "Helvetica-Oblique";
      else if (isbold)
        fontname = "Helvetica-Bold";
      else
        fontname = "Helvetica";
    }
  return fontname;
}

static void
escape_character (const std::string chr, std::string& str)
{
  std::size_t idx = str.find (chr);
  while (idx != std::string::npos)
    {
      str.insert (idx, "\\");
      idx = str.find (chr, idx + 2);
    }
}

namespace octave
{
  Matrix
  gl2ps_renderer::render_text (const std::string& txt,
                               double x, double y, double z,
                               int ha, int va, double rotation)
  {
    std::string saved_font = fontname;

    if (txt.empty ())
      return Matrix (1, 4, 0.0);

    // We have no way to get a bounding box from gl2ps, so we parse the raw
    // string using freetype
    Matrix bbox;
    std::string str = txt;
    std::list<octave::text_renderer::string> lst;

    text_to_strlist (str, lst, bbox, ha, va, rotation);

    // When using "tex" or when the string has only one line and no
    // special characters, use gl2ps for alignment
    if (lst.empty () || term.find ("tex") != std::string::npos
        || (lst.size () == 1 && ! lst.front ().get_code ()))
      {
        std::string name = fontname;
        int sz = fontsize;
        if (! lst.empty () && term.find ("tex") == std::string::npos)
          {
            octave::text_renderer::string s = lst.front ();
            name = select_font (s.get_name (), s.get_weight () == "bold",
                                s.get_angle () == "italic");
            set_color (s.get_color ());
            str = s.get_string ();
            sz = s.get_size ();
          }

        glRasterPos3d (x, y, z);

        // Escape parentheses until gl2ps does it (see bug #45301).
        if (term.find ("svg") == std::string::npos
            && term.find ("tex") == std::string::npos)
          {
            escape_character ("(", str);
            escape_character (")", str);
          }

        gl2psTextOpt (str.c_str (), name.c_str (), sz,
                      alignment_to_mode (ha, va), rotation);
        return bbox;
      }

    // Translate and rotate coordinates in order to use bottom-left alignment
    fix_strlist_position (x, y, z, bbox, rotation, lst);

    for (const auto& txtobj : lst)
      {
        fontname = select_font (txtobj.get_name (),
                                txtobj.get_weight () == "bold",
                                txtobj.get_angle () == "italic");
        if (txtobj.get_code ())
          {
            // This is only one character represented by a uint32 (utf8) code.
            // We replace it by the corresponding character in the
            // "Symbol" font except for svg which has built-in utf8 support.
            if (term.find ("svg") == std::string::npos)
              {
                fontname = "Symbol";
                str = code_to_symbol (txtobj.get_code ());
              }
            else
              {
                std::stringstream ss;
                ss << txtobj.get_code ();
                str = "&#" + ss.str () + ";";
              }
          }
        else
          {
            str = txtobj.get_string ();
            // Escape parenthesis until gl2ps does it (see bug ##45301).
            if (term.find ("svg") == std::string::npos)
              {
                escape_character ("(", str);
                escape_character (")", str);
              }
          }

        set_color (txtobj.get_color ());
        glRasterPos3d (txtobj.get_x (), txtobj.get_y (), txtobj.get_z ());
        gl2psTextOpt (str.c_str (), fontname.c_str (), txtobj.get_size (),
                      GL2PS_TEXT_BL, rotation);
      }

    fontname = saved_font;
    return bbox;
  }

  void
  gl2ps_renderer::set_font (const base_properties& props)
  {
    octave::opengl_renderer::set_font (props);

    // Set the interpreter so that text_to_pixels can parse strings properly
    if (props.has_property ("interpreter"))
      set_interpreter (props.get ("interpreter").string_value ());

    fontsize = props.get ("__fontsize_points__").double_value ();

    caseless_str fn = props.get ("fontname").xtolower ().string_value ();
    bool isbold =
      (props.get ("fontweight").xtolower ().string_value () == "bold");
    bool isitalic =
      (props.get ("fontangle").xtolower ().string_value () == "italic");

    fontname = select_font (fn, isbold, isitalic);
  }

  void
  gl2ps_renderer::draw_pixels (int w, int h, const float *data)
  {
    // Clip data between 0 and 1 for float values
    OCTAVE_LOCAL_BUFFER (float, tmp_data, 3*w*h);

    for (int i = 0; i < 3*h*w; i++)
      tmp_data[i] = (data[i] < 0.0f ? 0.0f : (data[i] > 1.0f ? 1.0f : data[i]));

    gl2psDrawPixels (w, h, 0, 0, GL_RGB, GL_FLOAT, tmp_data);
  }

  void
  gl2ps_renderer::draw_pixels (int w, int h, const uint8_t *data)
  {
    // gl2psDrawPixels only supports the GL_FLOAT type.

    OCTAVE_LOCAL_BUFFER (float, tmp_data, 3*w*h);

    static const float maxval = std::numeric_limits<float>::max ();

    for (int i = 0; i < 3*w*h; i++)
      tmp_data[i] = data[i] / maxval;

    draw_pixels (w, h, tmp_data);
  }

  void
  gl2ps_renderer::draw_pixels (int w, int h, const uint16_t *data)
  {
    // gl2psDrawPixels only supports the GL_FLOAT type.

    OCTAVE_LOCAL_BUFFER (float, tmp_data, 3*w*h);

    static const float maxval = std::numeric_limits<float>::max ();

    for (int i = 0; i < 3*w*h; i++)
      tmp_data[i] = data[i] / maxval;

    draw_pixels (w, h, tmp_data);
  }

  void
  gl2ps_renderer::draw_text (const text::properties& props)
  {
    if (props.get_string ().is_empty ())
      return;

    // First set font properties: freetype will use them to compute
    // coordinates and gl2ps will retrieve the color directly from the
    // feedback buffer
    set_font (props);
    set_color (props.get_color_rgb ());

    std::string saved_font = fontname;

    // Alignment
    int halign = 0;
    int valign = 0;

    if (props.horizontalalignment_is ("center"))
      halign = 1;
    else if (props.horizontalalignment_is ("right"))
      halign = 2;

    if (props.verticalalignment_is ("top"))
      valign = 2;
    else if (props.verticalalignment_is ("baseline"))
      valign = 3;
    else if (props.verticalalignment_is ("middle"))
      valign = 1;

    // FIXME: handle margin and surrounding box
    // Matrix bbox;

    const Matrix pos = get_transform ().scale (props.get_data_position ());
    std::string str = props.get_string ().string_vector_value ().join ("\n");

    render_text (str, pos(0), pos(1), pos.numel () > 2 ? pos(2) : 0.0,
                 halign, valign, props.get_rotation ());
  }

}

static void
safe_pclose (FILE *f)
{
  if (f)
    octave_pclose (f);
}

static void
safe_fclose (FILE *f)
{
  if (f)
    std::fclose (f);
}

#endif

namespace octave
{

  // If the name of the stream begins with '|', open a pipe to the command
  // named by the rest of the string.  Otherwise, write to the named file.

  void
  gl2ps_print (const graphics_object& fig, const std::string& stream,
               const std::string& term)
  {
#if defined (HAVE_GL2PS_H) && defined (HAVE_OPENGL)

    // FIXME: should we have a way to create a file that begins with the
    // character '|'?

    bool have_cmd = stream.length () > 1 && stream[0] == '|';

    FILE *fp = nullptr;

    octave::unwind_protect frame;

    if (have_cmd)
      {
        // Create process and pipe gl2ps output to it.

        std::string cmd = stream.substr (1);

        fp = octave_popen (cmd.c_str (), "w");

        if (! fp)
          error ("print: failed to open pipe \"%s\"", stream.c_str ());

        frame.add_fcn (safe_pclose, fp);
      }
    else
      {
        // Write gl2ps output directly to file.

        fp = std::fopen (stream.c_str (), "w");

        if (! fp)
          error ("gl2ps_print: failed to create file \"%s\"", stream.c_str ());

        frame.add_fcn (safe_fclose, fp);
      }

    gl2ps_renderer rend (fp, term);

    rend.draw (fig, stream);

    // Make sure buffered commands are finished!!!
    rend.finish ();

#else
    octave_unused_parameter (fig);
    octave_unused_parameter (stream);
    octave_unused_parameter (term);

    err_disabled_feature ("gl2ps_print", "gl2ps");
#endif
  }
}
