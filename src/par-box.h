#ifndef PAR_BOX_H
#define PAR_BOX_H

#include <Rcpp.h>
using namespace Rcpp;

#include <list>
#include <cmath>
#include <algorithm>
using namespace std;

#include "grid.h"
#include "layout.h"
#include "glue.h"
#include "penalty.h"


/* The ParBox class takes a list of boxes and lays them out
 * horizontally, breaking lines if necessary. The reference point
 * is the left end point of the baseline of the last line.
 */

template <class Renderer>
class ParBox : public Box<Renderer> {
private:
  BoxList<Renderer> m_nodes;
  Length m_vspacing;
  Length m_hspacing;
  Length m_width;
  Length m_ascent;
  Length m_descent;
  Length m_voff;
  // vertical shift if paragraph contains more than one line; is used to make sure the
  // bottom line in the box is used as the box baseline (all lines above are folded
  // into the ascent)
  Length m_multiline_shift;
  // calculated left baseline corner of the box after layouting
  Length m_x, m_y;

  vector<Length> m_sum_widths, m_sum_stretch, m_sum_shrink;

  // internal class representing an active breakpoint
  struct Breakpoint {
    size_t position, line;
    int fitness_class;
    Length totalwidth, totalstretch, totalshrink;
    int demerits;
    Breakpoint* previous;

    Breakpoint(size_t _position, size_t _line, int _fitness_class,
               Length _totalwidth, Length _totalstretch, Length _totalshrink,
               int _demerits, Breakpoint* _previous = nullptr) :
      position(_position), line(_line), fitness_class(_fitness_class),
      totalwidth(_totalwidth), totalstretch(_totalstretch),
      totalshrink(_totalshrink), demerits(_demerits), previous(_previous)
    {};
  };

  using BreakpointList = list<Breakpoint>;
  using BreaksList = vector<size_t>;

  // convenience function
  void add_active_node(BreakpointList &active_nodes, const Breakpoint &node) {
    // find the first position at which the line number of the new node
    // exceeds the line number of the node in the list
    auto i_node = active_nodes.begin();
    while (i_node != active_nodes.end() && i_node->line < node.line) {
      i_node++;
    }
    auto i_node_insert = i_node; // store insertion point

    // now check if there's another node with the same line number,
    // position, and fitness; if yes, drop the new node
    while (i_node != active_nodes.end() && i_node->line == node.line) {
      if (i_node->fitness_class == node.fitness_class && i_node->position == node.position) {
        return;
      }
      i_node++;
    }

    active_nodes.insert(i_node_insert, node);
  }

public:
  ParBox(const BoxList<Renderer>& nodes, Length vspacing, Length hspacing) :
    m_nodes(nodes), m_vspacing(vspacing), m_hspacing(hspacing),
    m_width(0), m_ascent(0), m_descent(0), m_voff(0),
    m_x(0), m_y(0) {
  }
  ~ParBox() {};

  Length width() { return m_width; }
  Length ascent() { return m_ascent; }
  Length descent() { return m_descent; }
  Length voff() { return m_voff; }

  void calc_layout(Length width_hint, Length height_hint) {
    // x and y offset as we layout
    Length x_off = 0, y_off = 0;

    int lines = 0;
    Length ascent = 0;
    Length descent = 0;

    for (auto i_node = m_nodes.begin(); i_node != m_nodes.end(); i_node++) {
      NodeType nt = (*i_node)->type();

      if (nt == NodeType::box) {
        // we propagate width and height hints to all child nodes,
        // in case they are useful there
        (*i_node)->calc_layout(width_hint, height_hint);
        if (x_off + (*i_node)->width() > width_hint) { // simple wrapping, no fancy logic
          x_off = 0;
          y_off = y_off - m_vspacing;
          lines += 1;
          descent = 0; // reset descent when starting new line
          // we don't reset ascent because we only record it for the first line
        }
        (*i_node)->place(x_off, y_off);
        x_off += (*i_node)->width();
        // add space, this needs to be replaced by glue
        x_off += m_hspacing;

        // record ascent and descent
        if ((*i_node)->descent() > descent) {
          descent = (*i_node)->descent();
        }
        if (lines == 0 && (*i_node)->ascent() > ascent) {
          ascent = (*i_node)->ascent();
        }
      } else if (nt == NodeType::glue) {
        // not implemented
      }
    }
    m_multiline_shift = lines*m_vspacing; // multi-line boxes need to be shifted upwards
    m_ascent = ascent + m_multiline_shift;
    m_descent = descent;
    m_width = width_hint;
  }


  void place(Length x, Length y) {
    m_x = x;
    m_y = y;
  }

  void render(Renderer &r, Length xref, Length yref) {
    // render all grobs in the list
    for (auto i_node = m_nodes.begin(); i_node != m_nodes.end(); i_node++) {
      (*i_node)->render(r, xref + m_x, yref + m_voff + m_y + m_multiline_shift);
    }
  }

  bool is_feasible_breakpoint(size_t i) {
    // we can break at position i if either i is a penalty less than infinity
    // or if it is a glue and the previous node is a box
    auto node = m_nodes[i];
    if (node->type() == NodeType::penalty) {
      if (static_cast<Penalty<Renderer>*>(node)->penalty() < Penalty<Renderer>::infinity) {
        return true;
      }
    }
    else if (i > 0 && node->type() == NodeType::glue) {
      if (m_nodes[i-1]->type() == NodeType::box) {
        return true;
      }
    }
    return false;
  }

  bool is_forced_break(size_t i) {
    // a penalty of -infinity is a forced break
    auto node = m_nodes[i];
    if (node->type() == NodeType::penalty) {
      if (static_cast<Penalty<Renderer>*>(node)->penalty() <= -1*Penalty<Renderer>::infinity) {
        return true;
      }
    }
    return false;
  }

  Length measure_width(size_t i1, size_t i2) {
    return m_sum_widths[i2] - m_sum_widths[i1];
  }

  Length measure_stretch(size_t i1, size_t i2) {
    return m_sum_stretch[i2] - m_sum_stretch[i1];
  }

  Length measure_shrink(size_t i1, size_t i2) {
    return m_sum_shrink[i2] - m_sum_shrink[i1];
  }

  double compute_adjustment_ratio(size_t i1, size_t i2, size_t line, const vector<Length> &line_lengths) {
    Length len = measure_width(i1, i2);

    // TODO: Are these two lines correct? They seem strange; needs to be checked.
    if (m_nodes[i2]->type() == NodeType::penalty) {
      len = len + m_nodes[i2]->width();
    }

    // we obtain the available length of the current line
    // from the vector of line lengths or, if we have used them up,
    // from the last line length
    Length len_avail;
    if (line < line_lengths.size()) {
      len_avail = line_lengths[line];
    } else {
      len_avail = line_lengths.back();
    }

    double r = 0; // adjustment ratio
    if (len < len_avail) { // if length is smaller than available length, need to stretch
      Length stretch =  measure_stretch(i1, i2);
      if (stretch > 0) {
        r = (len_avail - len)/stretch;
      } else {
        r = Glue<Renderer>::infinity;
      }
    } else if (len > len_avail) { // if length is larger than available length, need to shrink
      Length shrink =  measure_shrink(i1, i2);
      if (shrink > 0) {
        r = (len_avail - len)/shrink;
      } else {
        r = Glue<Renderer>::infinity; // TODO: Should this be -infinity?
      }
    }
    // r = 0 if len == len_avail

    return r;
  }

  // TODO: Do we really want to return a vector? Probably not.
  vector<size_t> compute_breaks(const vector<Length> &line_lengths, double tolerance = 1,
                                double fitness_demerit = 100, double flagged_demerit = 100) {
    size_t m = m_nodes.size();

    // if there are no nodes we have no breaks
    if (m == 0) {
      return BreaksList();
    }

    // set up vectors with the five possible values (w, y, z, p, f)
    // for each node
    vector<Length> w, y, z;
    vector<double> p;
    vector<bool> f;
    w.reserve(m);
    y.reserve(m);
    z.reserve(m);
    p.reserve(m);
    f.reserve(m);

    for (auto i_node = m_nodes.begin(); i_node != m_nodes.end(); i_node++) {
      w.push_back(i_node->width());
      if (i_node->type() == NodeType::glue) {
        y.push_back(static_cast<Glue<Renderer>*>(i_node)->stretch());
        z.push_back(static_cast<Glue<Renderer>*>(i_node)->shrink());
        p.push_back(0);
        f.push_back(false);
      } else if (i_node->type() == NodeType::penalty) {
        y.push_back(0);
        z.push_back(0);
        p.push_back(static_cast<Penalty<Renderer>*>(i_node)->penalty());
        f.push_back(static_cast<Penalty<Renderer>*>(i_node)->flagged());
      } else {
        y.push_back(0);
        z.push_back(0);
        p.push_back(0);
        f.push_back(false);
      }
    }

    // pre-compute sums
    m_sum_widths.resize(m);
    m_sum_stretch.resize(m);
    m_sum_shrink.resize(m);
    Length widths_sum = 0;
    Length stretch_sum = 0;
    Length shrink_sum = 0;
    for (size_t i = 0; i < m; i++) {
      m_sum_widths[i] = widths_sum;
      m_sum_stretch[i] = stretch_sum;
      m_sum_shrink[i] = shrink_sum;

      widths_sum = widths_sum + w[i];
      stretch_sum = stretch_sum + y[i];
      shrink_sum  = shrink_sum + z[i];
    }

    // set up list of active nodes, initialize with
    // break at beginning of text
    BreakpointList active_nodes;
    active_nodes.emplace_back(0, 0, 1, 0, 0, 0, 0);

    for (size_t i = 0; i < m; i++) {
      // we can only break at feasible breakpoints
      if (is_feasible_breakpoint(i)) {
        BreaksList breaks; // list of new possible breakpoints

        // iterate over all currently active nodes and evaluate breaking
        // between there and i

        // need to use a while loop because we modify the list as we iterate
        auto i_active = active_nodes.begin();
        while (i_active != active_nodes.end()) {
          double r = compute_adjustment_ratio(i_active->position, i, i_active->line, line_lengths);

          // remove active nodes when forced break
          // TODO: Do we have to prevent removal of the first node?
          // And what happens to the forced break node? How is it added?
          if (r < -1 || is_forced_break(i)) {
            i_active = active_nodes.erase(i_active); // this advances the iterator
            continue;
          }

          if (-1 <= r <= tolerance) {
            double demerits;

            // compute demeterits
            if (p[i] >= 0) {
              demerits = pow(1 + 100 * pow(abs(r), 3) + p[i], 3);
            } else if (is_forced_break(i)) {
              demerits = pow(1 + 100 * pow(abs(r), 3), 2) - pow(p[i], 2);
            } else {
              demerits = pow(1 + 100 * pow(abs(r), 3), 2);
            }

            // adjust demeteris for flagged items
            demerits = demerits + (flagged_demerit * f[i] * f[i_active->position]);

            // next, determine the fitness class of the line (very tight, tight, loose, etc)
            int fitness_class;
            if  (r < -.5) fitness_class = 0;
            else if (r <= .5) fitness_class = 1;
            else if (r <= 1) fitness_class = 2;
            else fitness_class = 3;

            // add demerits for changes in fitness class
            if (abs(fitness_class - i_active->fitness_class) > 1) {
              demerits = demerits + fitness_demerit;
            }

            // recrod feasible break from A to i
            breaks.emplace_back(
              i, i_active->line + 1, fitness_class,
              m_sum_widths[i], m_sum_stretch[i], m_sum_shrink[i],
              demerits
            );
          }
          i_active++;
        }
        // add all the new breaks to the list of active nodes
        for (auto i_brk = breaks.begin(); i_brk != breaks.end(); i_brk++) {
          add_active_node(active_nodes, *i_brk);
        }
      }

      // find the active node with the lowest number of demerits
      // TODO: handle empty list correctly
      // This relates to the removal of nodes for forced breaks above
      auto i_active = active_nodes.begin();
      double min_demerits = i_active->demerits;
      auto i_min = i_active;
      while (true) {
        // this assumes there is at least one node in the list
        i_active++;
        if (i_active == active_nodes.end()) {
          break;
        }
        if (i_active->demerits < min_demerits) {
          min_demerits = i_active->demerits;
          i_min = i_active;
        }
      }

      // now build a list of break points going backwards from minimum
      // demerits node to beginning
      vector<size_t> final_breaks;
      Breakpoint *p_node = i_min;
      while (p_node != nullptr) {
        final_breaks.push_back(p_node->position);
        p_node = p_node->previous;
      }
      reverse(final_breaks.begin(), final_breaks.end());

      // TODO: Need to return the final result. Also, where do we keep track of the
      // final r for each line? Currently it looks like we're throwing it away.
    }
  }
};

#endif
