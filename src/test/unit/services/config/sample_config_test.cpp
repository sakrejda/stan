#include <stan/services/sample/sample_config.hpp>
#include <gtest/gtest.h>

class Model {
};

TEST(sample_config, test) {
  Model model;
  
  stan::services::sample_config<Model> config(model);
  EXPECT_TRUE(config.validate());
}