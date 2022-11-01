#include <stdio.h>
#include <algorithm>

double convertAbsoluteIntoRelativeValue(
    double referenceValue,
    double value
) {
  const double weight = std::max(
      1.0, std::abs(referenceValue)
  );
  return value / weight;
}

int main() {
}