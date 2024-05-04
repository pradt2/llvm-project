struct Particle {
    double x[3];
    double d;
};

extern double sqrt(double);

void doWork(Particle *particles, int size) {

    [[clang::soa_conversion_target(particles)]]
    [[clang::soa_conversion_target_size(size)]]
    [[clang::soa_conversion_data_item("x[0]", "x[0]")]]
    [[clang::soa_conversion_data_item("x[1]", "x[1]")]]
    [[clang::soa_conversion_data_item("x[2]", "x[2]")]]
    [[clang::soa_conversion_data_item("d", "d")]]
    for (int i = 0; i < size; i++) {
        auto dist = sqrt(particles[i].x[0] * particles[i].x[1] * particles[i].x[2]);
        particles[i].d = dist;
    }
}
