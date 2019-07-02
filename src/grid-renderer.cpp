/* R bindings to grid renderer, for unit testing */

#include "grid-renderer.h"

// [[Rcpp::export]]
XPtr<GridRenderer> grid_renderer() {
  XPtr<GridRenderer> gr(new GridRenderer());

  return gr;
}

// [[Rcpp::export]]
void grid_renderer_text(XPtr<GridRenderer> gr, String label, Length x, Length y, List gp) {
  return gr->text(label, x, y, gp);
}

// [[Rcpp::export]]
List grid_renderer_text_details(XPtr<GridRenderer> gr, String label, List gp) {
  TextDetails td = gr->text_details(label, gp);

  List out = List::create(
    _["width_pt"] = td.width, _["ascent_pt"] = td.ascent,
    _["descent_pt"] = td.descent, _["space_pt"] = td.space
  );

  return out;
}

// [[Rcpp::export]]
void grid_renderer_rect(XPtr<GridRenderer> gr, Length x, Length y, Length width, Length height, List gp, Length r = 0) {
  return gr->rect(x, y, width, height, gp, r);
}

// [[Rcpp::export]]
List grid_renderer_collect_grobs(XPtr<GridRenderer> gr) {
  return gr->collect_grobs();
}

