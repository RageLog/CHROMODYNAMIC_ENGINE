// cd::cluster_fx umbrella — touches one symbol per member lib.
#include <cd/cluster_fx/cluster_fx.hpp>

int main()
{
    cd::render::cluster::ClusterConfig cfg { 4, 4, 4, 1.0472F, 16.0F/9.0F, 0.1F, 100.0F };
    cd::render::cluster::ClusterGrid grid { cfg };
    if (grid.total_cluster_count() != 64U) return 1;
    return 0;
}
