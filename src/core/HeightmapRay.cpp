#include "Heightmap.h"
#include <algorithm>
#include <cmath>
#include <functional>
namespace wb {
void HeightmapData::buildBounds() {
    bounds.clear();
    if (width < 2 || depth < 2)
        return;
    std::function<int(int, int, int, int)> add = [&](int x0, int z0, int x1, int z1) {
        int id = int(bounds.size());
        bounds.push_back({x0, z0, x1, z1, 65535, 0});
        if (x1 - x0 <= 8 && z1 - z0 <= 8) {
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) {
                    auto h = values[z * width + x];
                    bounds[id].low = std::min(bounds[id].low, h);
                    bounds[id].high = std::max(bounds[id].high, h);
                }
        } else {
            int xm = (x0 + x1) / 2, zm = (z0 + z1) / 2, n = 0;
            for (auto zr : {std::pair{z0, zm}, std::pair{zm, z1}})
                for (auto xr : {std::pair{x0, xm}, std::pair{xm, x1}})
                    if (xr.first < xr.second && zr.first < zr.second) {
                        int child = add(xr.first, zr.first, xr.second, zr.second);
                        bounds[id].children[n++] = child;
                        bounds[id].low = std::min(bounds[id].low, bounds[child].low);
                        bounds[id].high = std::max(bounds[id].high, bounds[child].high);
                    }
        }
        return id;
    };
    add(0, 0, width - 1, depth - 1);
}
bool HeightmapData::raySample(const std::array<double, 3> &o, const std::array<double, 3> &d, double &best,
                              double &outU, double &outV) const {
    bool hit = false;
    auto box = [&](double x0, double z0, double x1, double z1, double lo, double hi, double &near,
                   double &far) {
        near = 0;
        far = best;
        const double lower[3] = {x0, lo, z0}, upper[3] = {x1, hi, z1};
        for (int k = 0; k < 3; ++k) {
            if (std::abs(d[k]) < 1e-14) {
                if (o[k] < lower[k] - 1e-9 || o[k] > upper[k] + 1e-9)
                    return false;
            } else {
                double a = (lower[k] - o[k]) / d[k], b = (upper[k] - o[k]) / d[k];
                if (a > b)
                    std::swap(a, b);
                near = std::max(near, a);
                far = std::min(far, b);
            }
        }
        return near <= far + 1e-8;
    };
    const int nx = std::max(1, width - 1), nz = std::max(1, depth - 1);
    auto cell = [&](int x, int z) {
        const double x0 = double(x) / nx, z0 = double(z) / nz, x1 = double(x + 1) / nx,
                     z1 = double(z + 1) / nz;
        const double h00 = sample(x0, z0), h10 = sample(x1, z0), h01 = sample(x0, z1), h11 = sample(x1, z1);
        double near, far;
        if (!box(x0, z0, x1, z1, std::min({h00, h10, h01, h11}), std::max({h00, h10, h01, h11}), near, far))
            return;
        // Solve ray.y == bilinear height exactly within this cell, using t-relative coordinates.
        const double u = (o[0] + d[0] * near) * nx - x, v = (o[2] + d[2] * near) * nz - z, du = d[0] * nx,
                     dv = d[2] * nz;
        const double a = h10 - h00, b = h01 - h00, c = h11 - h10 - h01 + h00;
        const double A = c * du * dv, B = a * du + b * dv + c * (u * dv + v * du) - d[1];
        const double C = h00 + a * u + b * v + c * u * v - o[1] - d[1] * near;
        auto accept = [&](double t) {
            t += near;
            if (t < near - 1e-7 || t > far + 1e-7 || t >= best)
                return;
            best = std::max(0., t);
            outU = std::clamp(o[0] + d[0] * best, 0., 1.);
            outV = std::clamp(o[2] + d[2] * best, 0., 1.);
            hit = true;
        };
        if (std::abs(A) < 1e-14) {
            if (std::abs(B) > 1e-14)
                accept(-C / B);
            else if (std::abs(C) < 1e-8)
                accept(0);
        } else {
            double disc = B * B - 4 * A * C;
            if (disc >= -1e-12) {
                double root = std::sqrt(std::max(0., disc));
                accept((-B - root) / (2 * A));
                accept((-B + root) / (2 * A));
            }
        }
    };
    std::function<void(int)> visit = [&](int id) {
        const auto &n = bounds[id];
        double near, far;
        if (!box(double(n.x0) / nx, double(n.z0) / nz, double(n.x1) / nx, double(n.z1) / nz, n.low / 65535.,
                 n.high / 65535., near, far))
            return;
        if (n.children[0] < 0) {
            for (int z = n.z0; z < n.z1; ++z)
                for (int x = n.x0; x < n.x1; ++x)
                    cell(x, z);
        } else
            for (int child : n.children)
                if (child >= 0)
                    visit(child);
    };
    if (!bounds.empty())
        visit(0);
    else
        for (int z = 0; z < nz; ++z)
            for (int x = 0; x < nx; ++x)
                cell(x, z);
    return hit;
}
} // namespace wb
