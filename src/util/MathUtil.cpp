#include "MathUtil.h"

#include <cmath>
#include <stdexcept>
#include <algorithm>

using namespace std;

double MathUtil::Round(double x, int decimals) {
  double shift = pow(10.0, decimals);
  return (x >= 0)
    ? floor(x * shift + 0.5) / shift
    : ceil(x * shift - 0.5) / shift;
}

bool MathUtil::AlmostEqual(double x, double y, int precision) {
  return abs(x - y) < 5*pow(10.0, -(precision + 1));
}

// added by Yang
int MathUtil::factorial(int n) {
  int product = 1;
  for (int i = 1; i <= n; i++) { product *= i; }
  return product;
}

double MathUtil::LogNChooseK(int n, int k) {
	double result = 0;
	for (int i = 1; i <= k; i++) { result += log(n-i+1) - log(i); }
	return result;
}

double MathUtil::LogSumExp(std::vector<double>* log_values) {
	if (log_values->size() <= 0) { return 0.0; }
	else {
		double max_value = *max_element(log_values->begin(), log_values->end());
		double sum = 0;
		for (int i = 0; i < log_values->size() ; i++){ sum += exp(log_values->at(i) - max_value); }
		return log(sum) + max_value;
	}

}


MathUtil::Combination::Combination(size_t n, size_t k)
  : n_(n) {
  if (n < 0) {
    throw runtime_error("Combination constructed with negative parameter");
  }
  for (size_t i = 0; i < k; i++) {
    values_.push_back(i);
  }
}

size_t MathUtil::Combination::N() const {
  return n_;
}

size_t MathUtil::Combination::Size() const {
  return values_.size();
}

const vector<size_t>& MathUtil::Combination::Values() const {
  return values_;
}

bool MathUtil::Combination::HasNext() const {
  return !values_.empty() && values_.front() != n_ - values_.size();
}

void MathUtil::Combination::Next() {
  if (!HasNext()) {
    throw runtime_error("Combination does not have next");
  }
  int k = (int)Size();
  int i;
  for (i = k - 1; i > 0 && values_[i] == n_ - k + i; --i);
  ++values_[i];
  for (int j = i; j < k - 1; ++j) {
    values_[j + 1] = values_[j] + 1;
  }
}

MathUtil::MathUtil() {}
MathUtil::~MathUtil() {}

