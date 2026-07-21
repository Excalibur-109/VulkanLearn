#pragma once

// 聚合入口按依赖由低到高排列。每个子头也保持自包含，可以只引入需要的模块。
#include "Math/Scalar.hpp"
#include "Math/Vector.hpp"
#include "Math/Functions.hpp"
#include "Math/Curves.hpp"
#include "Math/Matrix.hpp"
#include "Math/Quaternion.hpp"
#include "Math/Random.hpp"
#include "Math/Color.hpp"
#include "Math/Rendering.hpp"
