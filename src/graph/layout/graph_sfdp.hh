// graph-tool -- a general graph modification and manipulation thingy
//
// Copyright (C) 2007-2011 Tiago de Paula Peixoto <tiago@skewed.de>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef GRAPH_FDP_HH
#define GRAPH_FDP_HH

#if (GCC_VERSION >= 40400)
#   include <tr1/random>
#else
#   include <boost/tr1/random.hpp>
#endif
#include <limits>
#include <iostream>

namespace graph_tool
{
using namespace std;
using namespace boost;

template <class Pos, class Weight>
class QuadTree
{
public:
    QuadTree(const Pos& ll, const Pos& ur, int max_level)
        :_ll(ll), _ur(ur), _cm(2, 0), _count(0),
         _max_level(max_level)
    {
    }

    QuadTree& get_leaf(size_t i)
    {
        if (_max_level > 0 && _leafs.empty())
        {
            _leafs.reserve(4);
            for (size_t i = 0; i < 4; ++i)
            {
                Pos lll = _ll, lur = _ur;
                if (i % 2)
                    lll[0] += (_ur[0] - _ll[0]) / 2;
                else
                    lur[0] -= (_ur[0] - _ll[0]) / 2;
                if (i / 2)
                    lll[1] += (_ur[1] - _ll[1]) / 2;
                else
                    lur[1] -= (_ur[1] - _ll[1]) / 2;
                _leafs.push_back(QuadTree(lll, lur, _max_level - 1));
            }
        }

        return _leafs[i];
    }

    vector<tuple<Pos,Weight> >& get_dense_leafs()
    {
        return _dense_leafs;
    }

    size_t put_pos(Pos& p, Weight w)
    {
        _count += w;
        _cm[0] += p[0] * w;
        _cm[1] += p[1] * w;

        if (_max_level == 0)
        {
            if (_count == w)
                _dense_leafs.reserve(10);
            _dense_leafs.push_back(make_tuple(p, w));
            return 1;
        }
        else
        {
            int i = p[0] > (_ll[0] + (_ur[0] - _ll[0]) / 2);
            int j = p[1] > (_ll[1] + (_ur[1] - _ll[1]) / 2);
            size_t sc = (_max_level > 0 && _leafs.empty()) ? 4 : 0;
            return sc + get_leaf(i + 2 * j).put_pos(p, w);
        }
        return 0;
    }

    void get_cm(Pos& cm)
    {
        for (size_t i = 0; i < 2; ++i)
            cm[i] = _cm[i] / _count;
    }

    double get_w()
    {
        return max(_ur[0] - _ll[0],
                   _ur[1] - _ll[1]);
    }

    Weight get_count()
    {
        return _count;
    }

    int max_level()
    {
        return _max_level;
    }

private:
    Pos _ll, _ur;
    vector<QuadTree> _leafs;
    vector<tuple<Pos,Weight> > _dense_leafs;
    Pos _cm;
    Weight _count;
    int _max_level;
};

template <class Pos>
inline double dist(const Pos& p1, const Pos& p2)
{
    double r = 0;
    for (size_t i = 0; i < 2; ++i)
        r += pow(p1[i] - p2[i], 2);
    return sqrt(r);
}

template <class Pos>
inline double f_r(double C, double K, double p, const Pos& p1, const Pos& p2)
{
    double d = dist(p1, p2);
    if (d == 0)
        return 0;
    return -C * pow(K, 1 + p) / pow(d, p);
}

template <class Pos>
inline double f_a(double K, const Pos& p1, const Pos& p2)
{
    return pow(dist(p1, p2), 2) / K;
}

template <class Pos>
inline double get_diff(const Pos& p1, const Pos& p2, Pos& r)
{
    double abs = 0;
    for (size_t i = 0; i < 2; ++i)
    {
        r[i] = p1[i] - p2[i];
        abs += pow(r[i], 2);
    }
    if (abs == 0)
        abs = 1;
    for (size_t i = 0; i < 2; ++i)
        r[i] /= sqrt(abs);
    return sqrt(abs);
}

template <class Pos>
inline double norm(Pos& x)
{
    double abs = 0;
    for (size_t i = 0; i < 2; ++i)
        abs += pow(x[i], 2);
    for (size_t i = 0; i < 2; ++i)
        x[i] /= sqrt(abs);
    return sqrt(abs);
}

struct get_sfdp_layout
{
    get_sfdp_layout(double C, double K, double p, double theta, double init_step,
                    double step_schedule, size_t max_level, double epsilon,
                    size_t max_iter, bool simple)
        : C(C), K(K), p(p), theta(theta), init_step(init_step),
          step_schedule(step_schedule), epsilon(epsilon),
          max_level(max_level), max_iter(max_iter), simple(simple) {}

    double C, K, p, theta, init_step, step_schedule, epsilon;
    size_t max_level, max_iter;
    bool simple;

    template <class Graph, class VertexIndex, class PosMap, class VertexWeightMap,
              class EdgeWeightMap, class PinMap>
    void operator()(Graph& g, VertexIndex vertex_index, PosMap pos,
                    VertexWeightMap vweight, EdgeWeightMap eweight, PinMap pin,
                    bool verbose) const
    {
        typedef typename property_traits<PosMap>::value_type pos_t;
        typedef typename property_traits<PosMap>::value_type::value_type val_t;

        typedef typename property_traits<VertexWeightMap>::value_type vweight_t;

        pos_t ll(2, numeric_limits<val_t>::max()),
            ur(2, -numeric_limits<val_t>::max());

        int i, N = num_vertices(g);
        #pragma omp parallel for default(shared) private(i)
        for (i = 0; i < N; ++i)
        {
            typename graph_traits<Graph>::vertex_descriptor v =
                vertex(i, g);
            if (v == graph_traits<Graph>::null_vertex())
                continue;
            pos[v].resize(2);

            for (size_t j = 0; j < 2; ++j)
            {
                #pragma omp critical
                ll[j] = min(pos[v][j], ll[j]);
                ur[j] = max(pos[v][j], ur[j]);
            }
        }

        val_t delta = epsilon * K + 1, E = 0, E0;
        E0 = numeric_limits<val_t>::max();
        size_t n_iter = 0;
        val_t step = init_step;
        size_t progress = 0;

        vector<reference_wrapper<QuadTree<pos_t, vweight_t> > > Q;
        Q.reserve(max_level * 2);

        while (delta > epsilon * K && (max_iter == 0 || n_iter < max_iter))
        {
            delta = 0;
            E0 = E;
            E = 0;

            pos_t nll(2, numeric_limits<val_t>::max()),
                nur(2, -numeric_limits<val_t>::max());

            QuadTree<pos_t, vweight_t> qt(ll, ur, max_level);
            for (i = 0; i < N; ++i)
            {
                typename graph_traits<Graph>::vertex_descriptor v =
                    vertex(i, g);
                if (v == graph_traits<Graph>::null_vertex())
                    continue;
                qt.put_pos(pos[v], vweight[v]);
            }

            pos_t diff(2, 0), pos_u(2, 0), ftot(2, 0), cm(2, 0);

            size_t nmoves = 0;
            #pragma omp parallel for default(shared) private(i)  \
                firstprivate(Q, diff, pos_u, ftot, cm) \
                reduction(+:E, delta, nmoves)
            for (i = 0; i < N; ++i)
            {
                typename graph_traits<Graph>::vertex_descriptor v =
                    vertex(i, g);
                if (v == graph_traits<Graph>::null_vertex())
                    continue;

                if (pin[v])
                    continue;

                ftot[0] = ftot[1] = 0;
                Q.clear();
                Q.push_back(ref(qt));
                while (!Q.empty())
                {
                    QuadTree<pos_t, vweight_t>& q = Q.back();
                    Q.pop_back();

                    if (q.max_level() == 0)
                    {
                        vector<tuple<pos_t,vweight_t> >& dleafs =
                            q.get_dense_leafs();
                        for(size_t j = 0; j < dleafs.size(); ++j)
                        {
                            val_t d = get_diff(get<0>(dleafs[j]), pos[v],
                                               diff);
                            if (d == 0)
                                continue;
                            val_t f = f_r(C, K, p, pos[v], get<0>(dleafs[j]));
                            f *= get<1>(dleafs[j]) * get(vweight, v);
                            for (size_t l = 0; l < 2; ++l)
                                ftot[l] += f * diff[l];
                        }
                    }
                    else
                    {
                        double w = q.get_w();
                        q.get_cm(cm);
                        double d = get_diff(cm, pos[v], diff);
                        if (w / d > theta)
                        {
                            for(size_t j = 0; j < 4; ++j)
                            {
                                QuadTree<pos_t, vweight_t>& leaf = q.get_leaf(j);
                                if (leaf.get_count() > 0)
                                    Q.push_back(ref(leaf));
                            }
                        }
                        else
                        {
                            if (d > 0)
                            {
                                val_t f = f_r(C, K, p, cm, pos[v]);
                                f *= q.get_count() * get(vweight, v);
                                for (size_t l = 0; l < 2; ++l)
                                    ftot[l] += f * diff[l];
                            }
                        }
                    }
                }

                typename graph_traits<Graph>::out_edge_iterator e, e_end;
                for (tie(e,e_end) = out_edges(v, g); e != e_end; ++e)
                {
                    typename graph_traits<Graph>::vertex_descriptor u =
                        target(*e, g);
                    if (u == v)
                        continue;
                    {
                        #pragma omp critical
                        for (size_t l = 0; l < 2; ++l)
                            pos_u[l] = pos[u][l];
                    }
                    get_diff(pos_u, pos[v], diff);
                    val_t f = f_a(K, pos_u, pos[v]);
                    f *= get(eweight, *e) * get(vweight, u) * get(vweight, v);
                    for (size_t l = 0; l < 2; ++l)
                        ftot[l] += f * diff[l];
                }

                E += pow(norm(ftot), 2);

                {
                    #pragma omp critical
                    for (size_t l = 0; l < 2; ++l)
                    {
                        ftot[l] *= step;
                        pos[v][l] += ftot[l];

                        nll[l] = min(pos[v][l], ll[l]);
                        nur[l] = max(pos[v][l], ur[l]);
                    }
                }
                delta += norm(ftot);
                nmoves++;
            }
            n_iter++;
            ll = nll;
            ur = nur;
            delta /= nmoves;

            if (verbose)
                cout << n_iter << " " << E << " " << step << " "
                     << delta << " " << max_level << endl;

            if (simple)
            {
                step *= step_schedule;
            }
            else
            {
                if (E < E0)
                {
                    ++progress;
                    if (progress >= 5)
                    {
                        progress = 0;
                        step /= step_schedule;
                    }
                }
                else
                {
                    progress = 0;
                    step *= step_schedule;
                }
            }
        }
    }
};

} // namespace graph_tool


#endif // GRAPH_FDP_HH
