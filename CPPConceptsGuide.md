# C++20 `<concepts>` 头文件详解

`<concepts>` 是 C++20 标准库头文件，提供一组可复用的**约束谓词**。它配合语言关键字 `concept`、`requires` 使用，为模板参数写出“必须满足什么能力”的接口契约。

它解决的不是运行时类型检查，而是模板实例化前的编译期约束选择与诊断问题。

```cpp
#include <concepts>

template <std::floating_point T>
T Half(T value) {
    return value / static_cast<T>(2);
}
```

调用 `Half(3.0F)` 合法；调用 `Half("text")` 时，编译器会在调用点报告 `T` 不满足 `std::floating_point`，而不是把错误拖到函数体里的除法表达式。

---

## 1. `<concepts>`、`concept`、`requires` 的关系

三者不是同一个东西。

| 项目 | 属于什么 | 作用 |
| --- | --- | --- |
| `<concepts>` | 标准库头文件 | 声明 `std::same_as`、`std::integral`、`std::convertible_to` 等标准概念。 |
| `concept` | C++20 语言关键字 | 定义自己的命名约束。 |
| `requires` | C++20 语言关键字 | 写约束子句，或写检查表达式/类型是否有效的 `requires` 表达式。 |
| `<type_traits>` | 标准库头文件 | 提供 `std::is_integral_v<T>` 等布尔 type trait。 |

所以即使不包含 `<concepts>`，语言仍然认识 `concept` 和 `requires`；只是无法使用 `std::integral` 这类标准库名称。反过来，仅包含 `<concepts>` 也不会自动让旧模板变成受约束模板，必须在声明中写出约束。

---

## 2. 为什么需要 Concepts

传统模板通常把限制藏在函数体、`static_assert` 或 SFINAE 中。

```cpp
template <typename T>
T Length(T value) {
    return value < 0 ? -value : value;
}
```

这个声明看起来接受任何 `T`。当传入不支持比较或一元负号的类型时，诊断会落在函数体深处，且可能出现许多模板实例化栈。

Concepts 把接口约束写到声明处：

```cpp
template <std::signed_integral T>
constexpr T Absolute(T value) noexcept {
    return value < 0 ? -value : value;
}
```

收益有四个：

- 接口直接表达语义，而不是让调用者猜测。
- 不满足约束的候选函数不会参与重载决议。
- 编译器会在调用点说明哪个约束为假。
- 多个重载都合法时，编译器可以依据约束的强弱选择更具体的版本。

Concept 仍然是编译期布尔条件，不会生成对象、字段、虚表或运行时分支。

---

## 3. 仓库中的实际用法

当前项目的核心定义在 [Math/Scalar.hpp](Math/Scalar.hpp)：

```cpp
template <typename T>
concept Scalar = std::is_arithmetic_v<T>;

template <typename T>
concept ArithmeticScalar = Scalar<T> &&
    !std::same_as<std::remove_cv_t<T>, bool>;

template <typename T>
concept FloatingScalar = std::floating_point<T>;

template <typename T>
concept IntegralScalar = std::integral<T> &&
    !std::same_as<std::remove_cv_t<T>, bool>;
```

这里的 `Scalar` 是项目语义，不是标准概念。它刻意采用 `std::is_arithmetic_v<T>`，因此包括整数、浮点和 `bool`；`ArithmeticScalar` 与 `IntegralScalar` 再把 `bool` 排除出去。

这是合理的领域建模：语言把 `bool` 归为 arithmetic/integral，但布尔值通常不应该参与 `Lerp`、`Square`、向量分量运算或插值。

```cpp
template <ArithmeticScalar T>
constexpr T Square(T value) noexcept {
    return value * value;
}

template <FloatingScalar T>
constexpr T Degrees(T radians) noexcept {
    return radians * (static_cast<T>(180) / Pi<T>);
}
```

`Square(true)` 会被项目约束拒绝；`Degrees(90)` 也会被拒绝，因为角度换算要求浮点结果。

矩阵构造函数使用了另一个标准概念，见 [Math/Matrix.hpp](Math/Matrix.hpp)：

```cpp
template <typename... Values>
    requires(sizeof...(Values) == R * C &&
             (std::convertible_to<Values, T> && ...))
explicit constexpr Matrix(Values... values) noexcept;
```

这里的折叠表达式表示：参数数量必须刚好等于矩阵元素数量，并且每个 `Values` 都必须可以转换为元素类型 `T`。因此 `Matrix<float, 2, 2>(1, 2.5, 3U, 4.0F)` 合法，而字符串或不兼容自定义类型会在构造调用点被排除。

### 3.1 头文件应直接包含它直接使用的声明

`Matrix.hpp` 当前通过 `Vector.hpp → Scalar.hpp` 间接获得了 `<concepts>`，所以 `std::convertible_to` 现在可以被识别。但 `Matrix.hpp` 自己直接写出了该名称；按照 include-what-you-use 原则，拥有这段声明的头文件应自行包含：

```cpp
#include "Math/Vector.hpp"

#include <concepts> // Matrix.hpp 直接使用 std::convertible_to。
```

这样未来即使 `Vector.hpp` 不再包含 `Scalar.hpp`，或者内部调整了包含顺序，`Matrix.hpp` 仍能单独编译。此处的说明不改变现有实现，只指出 `<concepts>` 在公共头文件中的正确依赖边界。

---

## 4. `<concepts>` 中最常用的概念

### 4.1 类型关系

| 概念 | 大意 | 适合的场景 |
| --- | --- | --- |
| `std::same_as<T, U>` | `T` 与 `U` 是完全相同的类型。 | 精确匹配特化分支。 |
| `std::derived_from<Derived, Base>` | `Derived` 公开且无歧义地派生自 `Base`。 | 接受基类接口或多态层次。 |
| `std::convertible_to<From, To>` | 隐式转换和 `static_cast` 都可行，且转换满足语义要求。 | 构造、数值输入、适配器。 |
| `std::common_reference_with<T, U>` | `T`、`U` 可得到共同引用类型。 | 泛型比较、迭代器引用处理。 |
| `std::common_with<T, U>` | `T`、`U` 可得到共同值类型并可相互转换。 | 泛型算法的统一值类型。 |

`std::same_as` 不会自动移除 `const`、引用或数组退化：

```cpp
static_assert(std::same_as<int, int>);
static_assert(!std::same_as<int, const int>);
static_assert(std::same_as<std::remove_cvref_t<const int&>, int>);
```

`std::derived_from` 比 `std::is_base_of_v<Base, Derived>` 更适合接口约束。后者在私有继承、保护继承或歧义多重继承时仍可能为真；前者还要求 `const volatile Derived*` 能转换为 `const volatile Base*`。

`std::convertible_to` 比只写 `std::is_convertible_v<From, To>` 更适合表达通用接口，因为它还要求 `static_cast<To>` 形式有效，并对转换结果提出语义上的相等性要求。对于纯数值构造，这正符合“每个输入都能转换为目标分量”的需求。

### 4.2 数值类别

| 概念 | 等价方向 | 说明 |
| --- | --- | --- |
| `std::integral<T>` | `std::is_integral_v<T>` | 整数类型，包括字符类型和 `bool`。 |
| `std::signed_integral<T>` | integral 且 signed | 有符号整数。 |
| `std::unsigned_integral<T>` | integral 且非 signed | 无符号整数；这里有 `bool` 陷阱。 |
| `std::floating_point<T>` | `std::is_floating_point_v<T>` | `float`、`double`、`long double` 及其 cv 形式。 |

最容易遗漏的一点是：`bool` 满足 `std::integral`，并且标准定义下也满足 `std::unsigned_integral`。

```cpp
static_assert(std::integral<bool>);
static_assert(std::unsigned_integral<bool>);
```

因此，如果算法的语义是“无符号计数、位掩码或尺寸”，而不是“任何非负整数”，应自行排除 `bool`：

```cpp
template <typename T>
concept UnsignedNumber = std::unsigned_integral<T> &&
    !std::same_as<std::remove_cv_t<T>, bool>;
```

当前项目的 `IsPowerOfTwo`、`NextPowerOfTwo` 等函数使用 `std::unsigned_integral`。这在常规 `u8/u16/u32/u64` 调用中完全正确；若接口要严格拒绝 `bool`，可改用上面的 `UnsignedNumber` 概念。

### 4.3 对象生命周期与赋值

| 概念 | 含义 |
| --- | --- |
| `std::destructible<T>` | 可以销毁，且析构满足约束。 |
| `std::constructible_from<T, Args...>` | 可以由 `Args...` 构造。 |
| `std::default_initializable<T>` | 可以按默认初始化规则构造。 |
| `std::move_constructible<T>` | 可移动构造并可转换为自身。 |
| `std::copy_constructible<T>` | 可复制构造、可从相关引用转换。 |
| `std::assignable_from<LHS, RHS>` | `LHS` 可由 `RHS` 赋值，且满足共同引用要求。 |
| `std::movable<T>` | 对象可移动、可赋值、可交换。 |
| `std::copyable<T>` | `movable` 加可复制构造/赋值。 |
| `std::semiregular<T>` | `copyable` 加默认初始化。 |
| `std::regular<T>` | `semiregular` 加相等可比较。 |

`std::regular` 很强，代表“像普通值一样工作”的类型。不要为了方便给数学函数加 `regular`：计算一个乘法通常只需要 `*`、转换和结果类型，要求默认构造、复制、相等比较反而会错误地拒绝合法类型。

### 4.4 可调用、比较与关系

| 概念 | 含义 |
| --- | --- |
| `std::invocable<F, Args...>` | `std::invoke(f, args...)` 这个表达式合法。 |
| `std::regular_invocable<F, Args...>` | `invocable` 且调用不会破坏 equality-preserving 语义。 |
| `std::predicate<F, Args...>` | 可调用，结果可作布尔判断。 |
| `std::relation<R, T, U>` | 可同时用于 `T,T`、`U,U`、`T,U`、`U,T` 的谓词。 |
| `std::equivalence_relation<R, T, U>` | 满足自反、对称、传递的等价关系。 |
| `std::strict_weak_order<R, T, U>` | 排序所需的严格弱序，例如比较器。 |
| `std::equality_comparable<T>` | 支持相等/不等比较，结果可布尔判断。 |
| `std::totally_ordered<T>` | 支持完整关系比较并满足总序语义。 |

这些概念检查的不只是表达式能否写出来；标准还规定了语义要求。例如 `strict_weak_order` 的比较器必须有稳定、可推理的排序关系。编译器能检查语法，不能自动证明比较器数学上真的满足传递性，所以测试仍然必要。

---

## 5. 四种写约束的方式

### 5.1 受约束模板参数

最简洁，适合一个主要类型参数：

```cpp
template <std::floating_point T>
constexpr T Radians(T degrees) noexcept;
```

也可以写成缩写函数模板：

```cpp
constexpr auto Twice(std::integral auto value) {
    return value * 2;
}
```

这等价于生成一个受 `std::integral` 约束的函数模板；它不是运行时多态。

### 5.2 尾随 `requires` 子句

适合依赖多个模板参数、`sizeof...`、非类型模板参数或返回类型条件：

```cpp
template <typename... Values>
    requires(sizeof...(Values) == 4 &&
             (std::convertible_to<Values, float> && ...))
void SetMatrix(Values... values);
```

类模板常把它放在模板参数列表之后：

```cpp
template <typename T, std::size_t N>
    requires std::floating_point<T> && (N >= 2 && N <= 4)
struct Vector;
```

成员函数可写在声明尾部：

```cpp
template <typename T>
struct Matrix {
    constexpr void Invert() requires std::floating_point<T>;
};
```

### 5.3 定义项目 Concept

把重复的业务语义命名，调用点会更清楚：

```cpp
template <typename T>
concept FloatingScalar = std::floating_point<T>;

template <FloatingScalar T>
constexpr T SmoothStep(T edge0, T edge1, T value) noexcept;
```

应该优先定义“小而准确”的概念。`FloatingScalar` 比 `NumberLike` 更好，因为前者明确说明 `SmoothStep` 依赖浮点除法、连续插值和浮点语义。

### 5.4 `requires` 表达式

`requires (...) { ... }` 用于检查类型、表达式和嵌套约束。它求值为编译期 `bool`。

```cpp
template <typename T>
concept Normalizable = requires(T value) {
    { value.Length() } -> std::floating_point;
    { value / value.Length() } -> std::same_as<T>;
};
```

`requires` 表达式的四类 requirement：

| 写法 | 检查内容 |
| --- | --- |
| `typename T::value_type;` | 类型 requirement：嵌套类型存在。 |
| `value.size();` | 简单 requirement：表达式合法。 |
| `{ value.size() } noexcept -> std::convertible_to<std::size_t>;` | 复合 requirement：表达式合法、是否 `noexcept`、返回类型满足什么概念。 |
| `requires sizeof(T) == 16;` | 嵌套 requirement：另一个常量表达式为真。 |

复合 requirement 中箭头右侧是**类型约束**，不是 C++ 函数返回类型箭头。上例的含义是：将表达式的 `decltype((value.size()))` 代入 `std::convertible_to<..., std::size_t>` 后必须满足。

---

## 6. 约束与重载：Subsumption

Concepts 的另一个关键能力是约束偏序。两个重载都匹配时，编译器会优先选择约束更强的候选。

```cpp
template <typename T>
concept Incrementable = requires(T value) { ++value; };

template <typename T>
concept Bidirectional = Incrementable<T> &&
    requires(T value) { --value; };

void Advance(Incrementable auto& value) {
    ++value;
}

void Advance(Bidirectional auto& value) {
    --value;
}
```

`Bidirectional<T>` 显式复用了 `Incrementable<T>`，所以它的约束包含后者。对同时满足两者的类型，第二个重载更受约束。

这也是为什么应复用命名 Concept，而不是在多个地方手写“看上去等价”的表达式。约束排序依赖规范化后的原子约束身份；文本相似不一定意味着编译器能把它们当作同一个约束。

```cpp
// 推荐：复用同一个原子约束。
template <typename T>
concept SignedInteger = std::signed_integral<T>;

// 避免在多个重载中复制 is_integral_v<T> && is_signed_v<T>。
```

---

## 7. Concepts 与 SFINAE、`if constexpr` 的分工

| 工具 | 适合解决的问题 |
| --- | --- |
| Concept / `requires` | 某类参数是否应该参与这个接口或重载。 |
| `if constexpr` | 参数已经合法，但不同类别需要不同实现分支。 |
| `static_assert` | 需要给出项目特定、强制性的额外诊断。 |
| `std::enable_if_t` | 维护 C++17 兼容代码，或无法改动旧接口时。新 C++20 代码优先 Concepts。 |

项目中的 `Abs` 是合理的 `if constexpr` 用法：输入已经被 `ArithmeticScalar` 限制为数值类型，函数内部只需要区分无符号类型不必取负的实现分支。

```cpp
template <ArithmeticScalar T>
constexpr T Abs(T value) noexcept {
    if constexpr (std::unsigned_integral<T>) {
        return value;
    } else {
        return value < static_cast<T>(0) ? -value : value;
    }
}
```

Concept 解决“这个函数是否适用”；`if constexpr` 解决“适用后如何实现”。

---

## 8. `<concepts>` 与 `<type_traits>` 的选择

`std::is_integral_v<T>` 和 `std::integral<T>` 在简单判断上接近，但使用位置不同：

```cpp
// trait：需要得到一个 bool 常量时很自然。
template <typename T>
inline constexpr bool IsFloat = std::is_floating_point_v<T>;

// concept：接口约束更自然，诊断和重载也更好。
template <std::floating_point T>
T Sqrt(T value);
```

以下场景仍然适合 trait：

- 计算类型变换，如 `std::remove_cv_t<T>`、`std::common_type_t<T, U>`。
- 在变量模板、`static_assert` 或 `if constexpr` 内做普通布尔判断。
- 标准库没有对应概念，而项目又不值得专门命名时。

以下场景优先 Concept：

- 函数、类模板、成员函数的公开可调用条件。
- 需要参与重载决议的条件。
- 希望诊断直接指出业务语义的条件。

两者可以组合，`ArithmeticScalar` 正是例子：它用 `<type_traits>` 中的 `std::is_arithmetic_v` 实现语义，再通过 `concept` 将它暴露为模板接口约束。

---

## 9. 设计准则与常见陷阱

### 9.1 约束语义，不是实现细节

如果函数只依赖浮点数的行为，使用 `std::floating_point`。不要因为当前实现碰巧调用了 `std::floor`，就把约束写成“必须是某个具体类”或“必须能转换成 `double`”。

### 9.2 不要用过宽的 `auto`

```cpp
// 不清楚接受什么，错误会晚出现。
auto Normalize(auto value);

// 接口契约明确。
template <FloatingScalar T>
constexpr T Normalize(T value);
```

缩写模板 `auto` 很方便，但公共数学 API 应有可读的语义约束。

### 9.3 不要误把隐式转换当成“数值兼容”

`std::convertible_to<Values, T>` 允许用户定义转换。对于矩阵逐元素构造这是合理的通用设计；但如果某个算法只接受同一种精度，应该使用 `std::same_as<std::remove_cvref_t<U>, T>` 或项目自己的精确概念。

### 9.4 Concept 不能证明全部语义

`std::totally_ordered` 不能证明用户定义的 `<` 真正满足数学总序；`std::regular_invocable` 不能阻止可调用对象修改外部全局状态。Concept 能检查表达式、类型关系与一部分规范化语义，运行时行为仍需测试和代码审查。

### 9.5 不要过度约束

一个只需要 `x + y` 的函数，不应该要求 `std::regular`；一个只需要无符号索引的函数，不一定要求浮点。约束越强，拒绝合法调用的风险越高。优先写“刚好足够”的最小语义。

---

## 10. 项目中可直接套用的模式

### 数值算法

```cpp
template <FloatingScalar T>
constexpr T EaseInOut(T value) noexcept {
    return value * value * (static_cast<T>(3) - static_cast<T>(2) * value);
}
```

### 固定维度模板

```cpp
template <FloatingScalar T, std::size_t N>
    requires(N >= 2 && N <= 4)
struct UnitVector;
```

### 接受可转换参数包

```cpp
template <FloatingScalar T, typename... Values>
    requires(sizeof...(Values) == 4 &&
             (std::convertible_to<Values, T> && ...))
constexpr auto MakeColor(Values... values);
```

### 检查自定义类型能力

```cpp
template <typename T>
concept VectorLike = requires(T value, std::size_t index) {
    typename T::ValueType;
    { T::Dimension } -> std::convertible_to<std::size_t>;
    { value[index] } -> std::convertible_to<typename T::ValueType>;
};
```

对于项目自身的 `Vector`、`Matrix` 等稳定类型，直接使用具体类型通常更简单。Concept 最有价值的场景是模板确实需要接受多个不同但能力相同的类型。

---

## 11. 最短记忆版

```cpp
#include <concepts>

template <std::floating_point T>
T Function(T value);                // 参数类别约束

template <typename T>
    requires std::integral<T>
T Other(T value);                   // requires 子句

template <typename T>
concept HasSize = requires(T value) {
    { value.size() } -> std::convertible_to<std::size_t>;
};                                  // 检查能力并命名
```

把 Concept 当作模板 API 的类型契约：先描述调用者必须提供的语义能力，再让函数体只处理满足契约的类型。
