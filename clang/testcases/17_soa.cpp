struct Particle {
    struct {
        double x[3];
    } force;
    double d;
};

extern double sqrt(double);

void doWork(Particle *particles, int size) {

    [[clang::soa_conversion_target(particles)]]
    [[clang::soa_conversion_target_size(size)]]
    [[clang::soa_conversion_data_item("force.x[0]", "force.x[0]")]]
    [[clang::soa_conversion_data_item("force.x[1]", "force.x[1]")]]
    [[clang::soa_conversion_data_item("force.x[2]", "force.x[2]")]]
    [[clang::soa_conversion_data_item("d", "d")]]
    for (int i = 0; i < size; i++) {
        auto dist = sqrt(particles[i].force.x[0] * particles[i].force.x[1] * particles[i].force.x[2]);
        particles[i].d = dist;
    }
}
