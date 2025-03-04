#include "geometry/Polygon2d.h"

#include <sstream>
#include <utility>
#include <cstddef>
#include <string>
#include <memory>

#include "geometry/Geometry.h"
#include "geometry/linalg.h"
#include "utils/printutils.h"
#ifdef ENABLE_MANIFOLD
#include "geometry/manifold/manifoldutils.h"
#endif
#include "geometry/cgal/cgalutils.h"
#include "Feature.h"
#include "geometry/PolySet.h"
#include "glview/RenderSettings.h"


Polygon2d::Polygon2d(Outline2d outline) : sanitized(true) {
  addOutline(std::move(outline));
}

std::unique_ptr<Geometry> Polygon2d::copy() const
{
  return std::make_unique<Polygon2d>(*this);
}

BoundingBox Outline2d::getBoundingBox() const {
  BoundingBox bbox;
  for (const auto& v : this->vertices) {
    bbox.extend(Vector3d(v[0], v[1], 0));
  }
  return bbox;
}

/*!
   Class for holding 2D geometry.

   This class will hold 2D geometry consisting of a number of closed
   polygons. Each polygon can contain holes and islands. Both polygons,
   holes and island contours may intersect each other.

   We can store sanitized vs. unsanitized polygons. Sanitized polygons
   will have opposite winding order for holes and is guaranteed to not
   have intersecting geometry. The winding order will be counter-clockwise
   for positive outlines and clockwise for holes. Sanitization is typically
   done by ClipperUtils, but if you create geometry which you know is sanitized,
   the flag can be set manually.
 */

size_t Polygon2d::memsize() const
{
  size_t mem = 0;
  for (const auto& o : this->outlines()) {
    mem += o.vertices.size() * sizeof(Vector2d) + sizeof(Outline2d);
  }
  mem += sizeof(Polygon2d);
  return mem;
}

BoundingBox Polygon2d::getBoundingBox() const
{
  BoundingBox bbox;
  for (const auto& o : this->outlines()) {
    bbox.extend(o.getBoundingBox());
  }
  return bbox;
}

std::string Polygon2d::dump() const
{
  std::ostringstream out;
  for (const auto& o : this->theoutlines) {
    out << "contour:\n";
    for (const auto& v : o.vertices) {
      out << "  " << v.transpose();
    }
    out << "\n";
  }
  return out.str();
}

bool Polygon2d::isEmpty() const
{
  return this->theoutlines.empty();
}

void Polygon2d::transform(const Transform2d& mat)
{
  if (mat.matrix().determinant() == 0) {
    LOG(message_group::Warning, "Scaling a 2D object with 0 - removing object");
    this->theoutlines.clear();
    return;
  }
  for (auto& o : this->theoutlines) {
    for (auto& v : o.vertices) {
      v = mat * v;
    }
  }
}

void Polygon2d::resize(const Vector2d& newsize, const Eigen::Matrix<bool, 2, 1>& autosize)
{
  auto bbox = this->getBoundingBox();

  // Find largest dimension
  int maxdim = (newsize[1] && newsize[1] > newsize[0]) ? 1 : 0;

  // Default scale (scale with 1 if the new size is 0)
  Vector2d scale(newsize[0] > 0 ? newsize[0] / bbox.sizes()[0] : 1,
                 newsize[1] > 0 ? newsize[1] / bbox.sizes()[1] : 1);

  // Autoscale where applicable
  double autoscale = newsize[maxdim] > 0 ? newsize[maxdim] / bbox.sizes()[maxdim] : 1;
  Vector2d newscale(!autosize[0] || (newsize[0] > 0) ? scale[0] : autoscale,
                    !autosize[1] || (newsize[1] > 0) ? scale[1] : autoscale);

  Transform2d t;
  t.matrix() <<
    newscale[0], 0, 0,
    0, newscale[1], 0,
    0, 0, 1;

  this->transform(t);
}

bool Polygon2d::is_convex() const
{
  if (theoutlines.size() > 1) return false;
  if (theoutlines.empty()) return true;

  auto const& pts = theoutlines[0].vertices;
  int N = pts.size();

  // Check for a right turn. This assumes the polygon is simple.
  for (int i = 0; i < N; ++i) {
    const auto& d1 = pts[(i + 1) % N] - pts[i];
    const auto& d2 = pts[(i + 2) % N] - pts[(i + 1) % N];
    double zcross = d1[0] * d2[1] - d1[1] * d2[0];
    if (zcross < 0) return false;
  }
  return true;
}

double Polygon2d::area() const
{
  auto ps = tessellate();
  if (ps == nullptr) {
    return 0;
  }

  double area = 0.0;
  for (const auto& poly : ps->indices) {
    const auto& v1 = ps->vertices[poly[0]];
    const auto& v2 = ps->vertices[poly[1]];
    const auto& v3 = ps->vertices[poly[2]];
    area += 0.5 * (
      v1.x() * (v2.y() - v3.y())
      + v2.x() * (v3.y() - v1.y())
      + v3.x() * (v1.y() - v2.y()));
  }
  return area;
}

/*!
   Triangulates this polygon2d and returns a 2D-in-3D PolySet.

   This is used for various purposes:
   * Geometry evaluation for roof, linear_extrude, rotate_extrude
   * Rendering (both preview and render mode)
   * Polygon area calculation
   *
   * One use-case is special: For geometry construction in Manifold mode, we require this function to
   * guarantee that vertices and their order are untouched (apart from adding a zero 3rd dimension)
   *
 */
std::unique_ptr<PolySet> Polygon2d::tessellate() const
{
  PRINTDB("Polygon2d::tessellate(): %d outlines", this->outlines().size());
#if defined(ENABLE_MANIFOLD) && defined(USE_MANIFOLD_TRIANGULATOR)
  if (RenderSettings::inst()->backend3D == RenderBackend3D::ManifoldBackend) {
    return ManifoldUtils::createTriangulatedPolySetFromPolygon2d(*this);
  }
  else
#endif
  return CGALUtils::createTriangulatedPolySetFromPolygon2d(*this);
}
