# C++ 常用关键字详解

本文以项目当前使用的 C++20 为基准，介绍日常开发中最常见的 C++ 关键字、使用场景和容易混淆的语义。涉及较新的语法时会标明对应的 C++ 版本。

## 1. 什么是关键字

关键字是由 C++ 语言保留、具有固定语法含义的单词，不能作为变量名、函数名、类型名或命名空间名。

```cpp
int value = 10;
// int 是关键字。
// value 是程序员定义的标识符。
```

以下内容经常出现在 C++ 代码中，但它们不是关键字：

| 写法 | 实际含义 |
|---|---|
| `lhs`、`rhs` | 普通变量名，分别是 left-hand side 和 right-hand side |
| `u8`、`u16`、`i32` | 项目定义的类型别名 |
| `std` | 标准库命名空间名 |
| `std::string` | 标准库类型 |
| `std::true_type` | 标准库类型，不是关键字 |
| `main` | 约定的程序入口函数名，不是关键字 |
| `override`、`final` | 具有特殊语法含义的标识符，通常称为上下文关键字 |
| `[[nodiscard]]` | 标准属性，不是关键字 |
| `#include`、`#define` | 预处理指令，不是 C++ 关键字 |

## 2. 常用关键字速查

| 分类 | 常用关键字 |
|---|---|
| 基本类型 | `bool`、`char`、`short`、`int`、`long`、`float`、`double`、`void` |
| 类型推导 | `auto`、`decltype` |
| 常量与编译期 | `const`、`constexpr`、`consteval`、`constinit`、`static_assert` |
| 存储和链接 | `static`、`extern`、`thread_local`、`inline` |
| 自定义类型 | `struct`、`class`、`union`、`enum` |
| 访问控制 | `public`、`protected`、`private`、`friend` |
| 函数和对象 | `return`、`this`、`explicit`、`virtual`、`noexcept`、`operator` |
| 控制流 | `if`、`else`、`switch`、`case`、`for`、`while`、`break`、`continue` |
| 模板和约束 | `template`、`typename`、`concept`、`requires` |
| 类型转换 | `static_cast`、`dynamic_cast`、`const_cast`、`reinterpret_cast` |
| 内存和布局 | `new`、`delete`、`sizeof`、`alignof`、`alignas` |
| 异常 | `try`、`catch`、`throw` |
| 协程 | `co_await`、`co_yield`、`co_return` |
| 模块 | `module`、`import`、`export` |

## 3. 基本类型关键字

### 3.1 整数类型

```cpp
bool enabled = true;
char letter = 'A';
short smallValue = 10;
int value = 20;
long largeValue = 30L;
long long veryLargeValue = 40LL;
```

整数类型还可以配合 `signed` 和 `unsigned`：

```cpp
signed int signedValue = -10;
unsigned int unsignedValue = 10U;
unsigned long long mask = 1ULL << 40;
```

需要注意，C++ 只规定这些类型的最小范围，没有规定它们在所有平台上的字节数完全一致。需要固定宽度时应使用 `<cstdint>`：

```cpp
#include <cstdint>

std::uint8_t byteValue = 0;
std::uint16_t formatValue = 0;
std::int32_t signedIndex = -1;
```

项目中的 `u8`、`u16`、`u32`、`u64` 和 `i32` 就是这类类型的别名，它们自身不是关键字。

### 3.2 字符类型

```cpp
char ascii = 'A';
wchar_t wide = L'中';
char8_t utf8CodeUnit = u8'A';       // C++20
char16_t utf16CodeUnit = u'A';
char32_t utf32CodeUnit = U'A';
```

这些类型表示编码单元，不保证一个对象就能表示一个完整的 Unicode 字符。

### 3.3 浮点类型

```cpp
float roughness = 0.5F;
double distance = 1000.0;
long double preciseValue = 1.0L;
```

`float` 通常是 32 位，`double` 通常是 64 位，但仍应以目标平台和编译器实现为准。

### 3.4 `void`

`void` 表示“没有值”或“未指定具体对象类型”。

```cpp
void Update() {
}

void* rawAddress = nullptr;
```

`void*` 可以保存对象地址，但在解引用前必须转换成具体指针类型。标准 C++ 不保证函数指针与 `void*` 之间的转换可移植，它也不会记录原始对象的类型信息。

### 3.5 `true`、`false` 和 `nullptr`

```cpp
bool visible = true;
bool finished = false;
int* pointer = nullptr;
```

`nullptr` 的类型是 `std::nullptr_t`，比旧式的 `0` 或 `NULL` 更安全：

```cpp
void SetValue(int value);
void SetValue(int* value);

SetValue(nullptr); // 明确选择指针重载。
```

## 4. 类型推导关键字

### 4.1 `auto`

`auto` 让编译器根据初始化表达式推导类型：

```cpp
auto count = 10;        // int
auto scale = 1.0F;      // float
auto name = "Vulkan";  // const char*
```

默认情况下，`auto` 会移除顶层 `const` 和引用：

```cpp
const int source = 10;

auto copy = source;         // int
const auto constant = source; // const int
const auto& reference = source; // const int&
```

范围 `for` 中通常使用引用，避免复制元素：

```cpp
for (const auto& texture : textures) {
    Use(texture);
}
```

C++20 允许在普通函数参数中使用 `auto`，它实际上是简写的函数模板：

```cpp
void Print(const auto& value) {
    std::cout << value;
}
```

### 4.2 `decltype`

`decltype` 获取表达式的声明类型，不执行该表达式：

```cpp
int value = 10;
decltype(value) copy = 20; // int
```

带括号时需要特别小心：

```cpp
int value = 10;

decltype(value) a = value;   // int
decltype((value)) b = value; // int&，因为 (value) 是左值表达式
```

`decltype(auto)` 会按照 `decltype` 的规则推导返回类型，常用于精确保留引用：

```cpp
decltype(auto) GetElement(std::vector<int>& values, std::size_t index) {
    return (values[index]); // 返回 int&。
}
```

## 5. 常量、限定符与编译期关键字

### 5.1 `const`

`const` 表示对象不能通过当前访问路径被修改：

```cpp
const int maxCount = 64;
```

指针中的位置决定限定对象：

```cpp
const int* pointerToConst = nullptr; // 不能通过指针修改 int。
int* const constPointer = nullptr;   // 指针本身不能改指向。
const int* const bothConst = nullptr;
```

成员函数末尾的 `const` 表示不修改对象的普通成员：

```cpp
class Texture {
public:
    int Width() const noexcept {
        return width_;
    }

private:
    int width_ = 0;
};
```

### 5.2 `volatile`

`volatile` 告诉编译器，对象可能被当前代码之外的机制修改，因此某些读取和写入不能被普通方式优化掉。

```cpp
volatile std::uint32_t* hardwareRegister = GetRegisterAddress();
std::uint32_t state = *hardwareRegister;
```

它常用于内存映射硬件寄存器。`volatile` 不是线程同步工具，不能替代 `std::atomic`、互斥锁或内存屏障。

### 5.3 `mutable`

`mutable` 允许成员在 `const` 成员函数中被修改，常用于缓存和互斥锁：

```cpp
class Resource {
public:
    std::size_t Hash() const {
        if (!cached_) {
            cachedHash_ = CalculateHash();
            cached_ = true;
        }
        return cachedHash_;
    }

private:
    mutable bool cached_ = false;
    mutable std::size_t cachedHash_ = 0;
};
```

Lambda 的 `mutable` 表示可以修改按值捕获的副本：

```cpp
auto counter = [value = 0]() mutable {
    return ++value;
};
```

### 5.4 `constexpr`

`constexpr` 表示对象或函数可以参与常量表达式计算。

```cpp
constexpr int Square(int value) noexcept {
    return value * value;
}

constexpr int result = Square(4);
static_assert(result == 16);
```

`constexpr` 函数不保证每次都在编译期执行：

```cpp
int runtimeValue = ReadInput();
int result = Square(runtimeValue); // 可以在运行期执行。
```

### 5.5 `consteval`

`consteval` 定义立即函数，每一次可能求值的调用都必须在编译期完成：

```cpp
consteval int BuildVersion(int major, int minor) {
    return major * 100 + minor;
}

constexpr int version = BuildVersion(1, 5);
```

如果参数只能在运行期获得，调用会编译失败。

### 5.6 `constinit`

`constinit` 要求具有静态或线程存储期的变量执行静态初始化，避免静态初始化顺序问题。它不表示对象不可修改。

```cpp
constinit int globalCounter = 0;

void Increment() {
    ++globalCounter; // 合法。
}
```

三者的核心区别：

| 关键字 | 主要保证 |
|---|---|
| `constexpr` | 对象是常量，函数可以用于常量表达式 |
| `consteval` | 函数调用必须在编译期求值 |
| `constinit` | 静态或线程变量必须静态初始化，但变量不一定是常量 |

### 5.7 `static_assert`

`static_assert` 在编译期验证条件：

```cpp
static_assert(sizeof(std::uint32_t) == 4);
static_assert(std::is_enum_v<RHIFormat>, "RHIFormat must be an enum");
```

条件为 `false` 时程序直接编译失败，不产生运行期开销。

## 6. 存储期、链接与声明关键字

### 6.1 `static`

`static` 根据位置有多种含义。

函数内的静态变量只初始化一次，并持续到程序结束：

```cpp
int NextId() {
    static int current = 0;
    return ++current;
}
```

类的静态成员属于类本身，不属于某个对象：

```cpp
class Device {
public:
    inline static int liveCount = 0;
};
```

命名空间作用域中的 `static` 给予名称内部链接，使名称只在当前翻译单元可见：

```cpp
static void InternalHelper() {
}
```

现代 C++ 中也可以使用匿名命名空间表达翻译单元私有实现。

### 6.2 `extern`

`extern` 通常声明对象或函数定义在其他翻译单元：

```cpp
// Config.hpp
extern int gFrameCount;

// Config.cpp
int gFrameCount = 0;
```

`extern "C"` 指定 C 语言链接规则，常用于调用 C API：

```cpp
extern "C" int NativeLibraryInit();
```

### 6.3 `thread_local`

`thread_local` 让每个线程拥有独立对象：

```cpp
thread_local std::uint32_t currentThreadFrame = 0;
```

它可以与 `static` 或 `extern` 一起使用，但其生命周期和初始化成本需要结合线程模型考虑。

### 6.4 `inline`

`inline` 的核心语言作用是允许满足规则的同一定义出现在多个翻译单元中，而不是强制编译器内联机器码。

```cpp
inline int Add(int lhs, int rhs) noexcept {
    return lhs + rhs;
}

inline constexpr int DefaultFrameCount = 2;
```

是否真正展开函数调用由优化器决定。

### 6.5 `using` 和 `typedef`

两者都可以创建类型别名：

```cpp
using u32 = std::uint32_t;
typedef std::uint32_t OldStyleU32;
```

`using` 更容易阅读，并且支持别名模板：

```cpp
template <typename T>
using DynamicArray = std::vector<T>;
```

`using` 还可以引入命名空间名称或基类重载：

```cpp
using std::string;

class Derived : public Base {
public:
    using Base::Update;
};
```

不要在公共头文件中使用 `using namespace ...;`，否则会把大量名称注入包含者的作用域。

### 6.6 `namespace`

命名空间用于组织名称并避免冲突：

```cpp
namespace rhi {

class RHIDevice {
};

} // namespace rhi
```

C++17 支持嵌套命名空间简写：

```cpp
namespace engine::render {
}
```

## 7. 自定义类型关键字

### 7.1 `struct` 和 `class`

两者都可以包含成员变量、成员函数、构造函数、虚函数和模板。核心差异只是默认访问权限和默认继承权限：

```cpp
struct PublicByDefault {
    int value = 0; // 默认 public。
};

class PrivateByDefault {
    int value_ = 0; // 默认 private。
};
```

工程中通常用 `struct` 表达数据聚合，用 `class` 表达带有不变量和封装行为的对象，但这只是编码习惯。

### 7.2 `enum`

普通枚举会把枚举项放入外围作用域，并且通常可以隐式转换成整数：

```cpp
enum Color {
    Red,
    Green
};
```

更推荐作用域枚举 `enum class`：

```cpp
enum class RHIFormat : u16 {
    Undefined,
    RGBA8_UNorm
};
```

这里的 `: u16` 不是继承，而是指定底层整数类型。枚举不能继承类或其他枚举。

使用枚举项时必须带作用域：

```cpp
RHIFormat format = RHIFormat::RGBA8_UNorm;
```

`enum class` 不会自动获得位运算能力。需要显式定义运算符或使用项目中的枚举 flags trait。

### 7.3 `union`

`union` 的所有非静态成员共享同一段内存，通常只有一个成员处于活动状态：

```cpp
union NumberBits {
    std::uint32_t integer;
    float floatingPoint;
};
```

直接读取非活动成员容易触发未定义行为。需要安全的多类型值时优先考虑 `std::variant`；需要按位转换时优先考虑 C++20 的 `std::bit_cast`。

### 7.4 `public`、`protected` 和 `private`

| 访问级别 | 当前类 | 派生类 | 外部代码 |
|---|---:|---:|---:|
| `public` | 可以 | 可以 | 可以 |
| `protected` | 可以 | 可以 | 不可以 |
| `private` | 可以 | 不可以直接访问 | 不可以 |

```cpp
class Resource {
public:
    void Reset();

protected:
    void NotifyBackend();

private:
    int handle_ = 0;
};
```

### 7.5 `friend`

`friend` 允许指定函数或类型访问当前类的非公有成员：

```cpp
class Handle {
    friend bool operator==(Handle lhs, Handle rhs) noexcept;

private:
    std::uint64_t value_ = 0;
};
```

友元关系不是继承关系，不会自动传递，也不是双向关系。

## 8. 函数与面向对象关键字

### 8.1 `return`

`return` 结束函数并可返回结果：

```cpp
int Add(int lhs, int rhs) {
    return lhs + rhs;
}

void Reset() {
    return;
}
```

不要返回局部对象的指针或引用：

```cpp
const int& Broken() {
    int value = 10;
    return value; // 错误：函数结束后 value 已销毁。
}
```

### 8.2 `this`

`this` 是非静态成员函数中的当前对象指针：

```cpp
class Counter {
public:
    Counter& Set(int value) {
        this->value_ = value;
        return *this;
    }

private:
    int value_ = 0;
};
```

静态成员函数没有 `this`。

### 8.3 `explicit`

`explicit` 阻止构造函数或转换函数参与不希望发生的隐式转换：

```cpp
class RHIHandle {
public:
    explicit RHIHandle(std::uint64_t value) : value_(value) {
    }

private:
    std::uint64_t value_ = 0;
};

RHIHandle handle{10}; // 正确。
// RHIHandle handle = 10; // 编译失败。
```

C++20 支持条件 `explicit`：

```cpp
template <typename T>
class Wrapper {
public:
    explicit(sizeof(T) > 8) Wrapper(T value);
};
```

### 8.4 `virtual`

`virtual` 开启运行期多态：

```cpp
class Backend {
public:
    virtual ~Backend() = default;
    virtual void Submit() = 0;
};
```

包含虚函数的基类如果会通过基类指针销毁派生对象，析构函数通常也必须是虚函数。

`override` 和 `final` 不是普通保留关键字，而是在特定位置具有特殊含义的标识符：

```cpp
class VulkanBackend final : public Backend {
public:
    void Submit() override;
};
```

- `override` 要求函数确实覆盖基类虚函数，签名不匹配会编译失败。
- `final` 用于虚函数时禁止继续覆盖，用于类时禁止继续派生。

### 8.5 `noexcept`

`noexcept` 表示函数承诺不让异常逃出：

```cpp
void Swap(Resource& lhs, Resource& rhs) noexcept;
```

如果异常逃出 `noexcept` 函数，程序会调用 `std::terminate`。

它也可以作为编译期运算符判断表达式是否承诺不抛异常：

```cpp
Resource lhs{};
Resource rhs{};
static_assert(noexcept(Swap(lhs, rhs)));
```

`noexcept` 不是“关闭异常检查”，而是函数接口和优化、容器移动策略的一部分。

### 8.6 `operator`

`operator` 用于声明重载运算符和转换函数：

```cpp
RHIFlags operator|(RHIFlags lhs, RHIFlags rhs) noexcept;

class Handle {
public:
    explicit operator bool() const noexcept {
        return value_ != 0;
    }

private:
    std::uint64_t value_ = 0;
};
```

运算符重载不能改变运算符的优先级、结合性或操作数数量。

### 8.7 `default` 和 `delete`

`default` 可以要求编译器生成特殊成员函数：

```cpp
class Resource {
public:
    Resource() = default;
    ~Resource() = default;
};
```

`delete` 可以禁止某个重载或特殊成员函数：

```cpp
class Resource {
public:
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
};
```

这里的 `delete` 与释放 `new` 创建的对象使用同一个关键字，但语法位置和含义不同。

## 9. 控制流关键字

### 9.1 `if` 和 `else`

```cpp
if (handle.isValid()) {
    Use(handle);
} else {
    ReportError();
}
```

C++17 支持带初始化语句的 `if`：

```cpp
if (auto iterator = resources.find(name); iterator != resources.end()) {
    Use(iterator->second);
}
```

`if constexpr` 在编译期丢弃不满足条件的模板分支：

```cpp
template <typename T>
void PrintValue(const T& value) {
    if constexpr (std::is_pointer_v<T>) {
        std::cout << *value;
    } else {
        std::cout << value;
    }
}
```

### 9.2 `switch`、`case` 和 `default`

```cpp
switch (api) {
case RHIGraphicsAPI::Vulkan:
    InitializeVulkan();
    break;
case RHIGraphicsAPI::D3D12:
    InitializeD3D12();
    break;
default:
    ReportUnsupportedApi();
    break;
}
```

没有 `break` 时会继续执行下一个分支。确实需要贯穿时应使用 `[[fallthrough]]` 表达意图。

### 9.3 `for`

传统 `for`：

```cpp
for (std::size_t index = 0; index < resources.size(); ++index) {
    Use(resources[index]);
}
```

范围 `for`：

```cpp
for (const Resource& resource : resources) {
    Use(resource);
}
```

### 9.4 `while` 和 `do`

```cpp
while (HasPendingWork()) {
    ProcessOne();
}

do {
    PollEvent();
} while (ShouldContinue());
```

`do-while` 至少执行一次循环体。

### 9.5 `break` 和 `continue`

- `break` 结束当前循环或 `switch`。
- `continue` 跳过本轮剩余代码并开始下一轮循环。

```cpp
for (const Resource& resource : resources) {
    if (!resource.IsValid()) {
        continue;
    }
    if (resource.Id() == targetId) {
        break;
    }
}
```

### 9.6 `goto`

`goto` 跳转到当前函数内的标签：

```cpp
if (!Initialize()) {
    goto cleanup;
}

cleanup:
ReleaseTemporaryState();
```

普通 C++ 代码应优先使用 RAII、函数拆分和结构化控制流。`goto` 偶尔用于非常底层的统一错误清理，但不能跳过需要初始化的对象进入其作用域。

## 10. 模板和约束关键字

### 10.1 `template`

`template` 声明函数模板、类模板或变量模板：

```cpp
template <typename T>
T Max(T lhs, T rhs) {
    return lhs < rhs ? rhs : lhs;
}
```

显式特化为特定类型提供实现：

```cpp
template <typename Enum>
struct EnableFlags : std::false_type {};

template <>
struct EnableFlags<RHIRenderFeature> : std::true_type {};
```

这里继承 `std::true_type` 的是 trait 特化，不是 `RHIRenderFeature` 枚举本身。

### 10.2 `typename`

模板参数中的 `typename` 表示参数是类型：

```cpp
template <typename T>
class Array {
};
```

在依赖名称前，`typename` 用于告诉编译器该名称是类型：

```cpp
template <typename Container>
void Process(Container& container) {
    typename Container::value_type value{};
}
```

模板参数中的 `class` 与 `typename` 在大多数类型参数场景下等价。

### 10.3 `concept`

`concept` 为模板参数定义编译期约束：

```cpp
template <typename T>
concept FloatingPoint = std::is_floating_point_v<T>;

template <FloatingPoint T>
T Length(T value) {
    return value < 0 ? -value : value;
}
```

Concept 不是新类型，也不会生成运行期对象。它是一个编译期布尔约束。

### 10.4 `requires`

`requires` 可以应用 concept，也可以直接检查表达式是否有效：

```cpp
template <typename T>
concept HasSize = requires(const T& value) {
    { value.size() } -> std::convertible_to<std::size_t>;
};
```

也可以约束函数：

```cpp
template <typename T>
requires std::is_enum_v<T>
constexpr auto ToUnderlying(T value) noexcept {
    return static_cast<std::underlying_type_t<T>>(value);
}
```

约束失败通常意味着模板不参与重载选择，而不是在函数体深处产生难以理解的错误。

## 11. 类型转换关键字

### 11.1 `static_cast`

用于语言规则允许且相对明确的转换：

```cpp
float value = 3.5F;
int integer = static_cast<int>(value);

auto rawFormat = static_cast<u16>(RHIFormat::RGBA8_UNorm);
```

它常用于数值转换、枚举与底层类型转换、已知安全的继承层次转换。

### 11.2 `dynamic_cast`

用于多态继承体系中的运行期类型检查：

```cpp
Backend* backend = GetBackend();
if (auto* vulkan = dynamic_cast<VulkanBackend*>(backend)) {
    vulkan->GetNativeHandles();
}
```

基类必须是多态类型，通常意味着至少有一个虚函数。指针转换失败返回 `nullptr`，引用转换失败抛出 `std::bad_cast`。

### 11.3 `const_cast`

用于增加或移除 `const`/`volatile` 限定：

```cpp
const int* source = GetValue();
int* mutablePointer = const_cast<int*>(source);
```

如果原始对象实际定义为 `const`，通过移除限定后的指针修改它会产生未定义行为。正常业务代码很少需要 `const_cast`。

### 11.4 `reinterpret_cast`

执行底层表示层面的转换：

```cpp
std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
```

它不会自动解决对象生命周期、对齐、严格别名或平台 ABI 问题。序列化和位模式转换通常应使用 `std::memcpy` 或 `std::bit_cast`。

### 11.5 `typeid`

`typeid` 返回 `std::type_info`：

```cpp
const std::type_info& info = typeid(Resource);
```

对多态对象表达式使用时可以取得动态类型信息。普通业务分发通常优先考虑虚函数、`std::variant` 或明确的类型枚举。

## 12. 内存、对象生命周期和布局关键字

### 12.1 `new` 和 `delete`

```cpp
Resource* resource = new Resource{};
delete resource;

Resource* array = new Resource[16];
delete[] array;
```

`new` 同时负责分配存储和构造对象，`delete` 负责析构对象并释放对应存储。`new[]` 必须配对 `delete[]`。

现代 C++ 业务代码优先使用自动存储期对象和智能指针：

```cpp
auto resource = std::make_unique<Resource>();
```

这样可以利用 RAII 自动管理异常和提前返回路径上的资源释放。

### 12.2 `sizeof`

`sizeof` 返回类型或表达式占用的字节数，结果类型是 `std::size_t`：

```cpp
static_assert(sizeof(std::uint32_t) == 4);

Resource resources[8];
std::size_t byteCount = sizeof(resources);
```

对大多数表达式使用 `sizeof` 时表达式不会被求值。数组传入函数后通常退化为指针，此时 `sizeof` 得到的是指针大小，不是原数组大小。

### 12.3 `alignof` 和 `alignas`

`alignof` 查询类型的对齐要求：

```cpp
std::size_t alignment = alignof(float4x4);
```

`alignas` 指定对象或类型至少需要的对齐：

```cpp
struct alignas(16) UniformBufferObject {
    float values[4];
};
```

这在 SIMD、GPU constant/uniform buffer 和平台 ABI 交互中很常见。对齐正确不代表 C++ 与 shader 的字段布局必然完全相同，还必须核对各自的布局规则。

## 13. 异常关键字

### 13.1 `try`、`catch` 和 `throw`

```cpp
try {
    LoadPipeline();
} catch (const std::runtime_error& error) {
    Log(error.what());
}
```

抛出异常：

```cpp
if (!file.is_open()) {
    throw std::runtime_error("failed to open file");
}
```

重新抛出当前异常：

```cpp
catch (...) {
    CleanupDiagnosticState();
    throw;
}
```

通常按 `const` 引用捕获异常，避免复制和对象切片。析构函数默认不应让异常逃出。

## 14. 协程关键字

C++20 提供三个协程关键字：

| 关键字 | 含义 |
|---|---|
| `co_await` | 挂起协程并等待异步结果 |
| `co_yield` | 产生一个值并挂起协程 |
| `co_return` | 结束协程并可返回结果 |

示意代码：

```cpp
Task<Texture> LoadTextureAsync() {
    FileData data = co_await ReadFileAsync();
    co_return DecodeTexture(data);
}
```

协程关键字只负责把函数转换成状态机。实际调度、线程切换、返回类型和生命周期由协程返回类型及其 `promise_type` 决定。

## 15. 模块关键字

C++20 模块使用 `module`、`import` 和 `export`：

```cpp
export module engine.math;

export int Add(int lhs, int rhs) {
    return lhs + rhs;
}
```

使用模块：

```cpp
import engine.math;
```

模块可以减少宏泄漏和重复解析头文件的问题，但实际可用性取决于编译器、构建系统和依赖库支持。本项目当前仍以头文件和源文件组织为主。

## 16. 运算符替代关键字

C++ 为部分符号运算符提供了关键字形式：

| 关键字 | 等价符号 |
|---|---|
| `and` | `&&` |
| `or` | `||` |
| `not` | `!` |
| `bitand` | `&` |
| `bitor` | `|` |
| `xor` | `^` |
| `compl` | `~` |
| `and_eq` | `&=` |
| `or_eq` | `|=` |
| `xor_eq` | `^=` |
| `not_eq` | `!=` |

```cpp
if (ready and not failed) {
    flags or_eq featureMask;
}
```

它们是真正的 C++ 关键字，不需要包含额外头文件。不过大多数项目仍统一使用符号形式。

## 17. 预处理指令不是关键字

预处理发生在主要的 C++ 语法分析之前：

```cpp
#include <vector>
#define RHI_ENABLE_VALIDATION 1

#if RHI_ENABLE_VALIDATION
void EnableValidation();
#endif
```

常用预处理指令包括：

| 指令 | 用途 |
|---|---|
| `#include` | 包含头文件 |
| `#define` | 定义宏 |
| `#undef` | 取消宏 |
| `#if`、`#elif`、`#else`、`#endif` | 条件编译 |
| `#ifdef`、`#ifndef` | 判断宏是否定义 |
| `#pragma` | 实现相关控制 |
| `#error` | 主动产生编译错误 |

宏没有类型、作用域和普通函数语义。能用 `constexpr`、模板、inline 函数或枚举表达的逻辑，通常不应优先使用宏。

## 18. 标准属性不是关键字

标准属性使用双中括号语法：

```cpp
[[nodiscard]] RHITexture CreateTexture();
[[deprecated("Use CreateTexture instead")]] RHITexture CreateOldTexture();
[[maybe_unused]] int debugValue = 0;
```

常用属性：

| 属性 | 含义 |
|---|---|
| `[[nodiscard]]` | 提醒调用者不要忽略结果 |
| `[[maybe_unused]]` | 允许实体暂时未使用 |
| `[[deprecated]]` | 标记接口已弃用 |
| `[[fallthrough]]` | 明确表示 `switch` 分支有意贯穿 |
| `[[likely]]`、`[[unlikely]]` | 提供分支可能性的优化提示 |

属性可以影响诊断或优化，但 `nodiscard`、`deprecated` 等名称不是关键字。

## 19. 结合本项目理解关键字

### 19.1 `enum class RHIFormat : u16`

```cpp
enum class RHIFormat : u16 {
    Undefined,
    RGBA8_UNorm
};
```

- `enum` 是关键字，用于定义枚举。
- `class` 在这里表示作用域枚举。
- `RHIFormat` 是类型名，不是关键字。
- `u16` 是项目类型别名，不是关键字。
- `: u16` 指定底层类型，不表示继承。

### 19.2 `std::true_type` 和 `std::false_type`

```cpp
template <typename Enum>
struct RHIEnableEnumFlags : std::false_type {};

template <>
struct RHIEnableEnumFlags<RHIRenderFeature> : std::true_type {};
```

- `template`、`typename` 和 `struct` 是关键字。
- `std::true_type` 和 `std::false_type` 是标准库类型。
- 这里是 trait 类型继承，和枚举继承无关。
- 特化后的 `::value` 在编译期决定枚举能否使用 flags 运算。

### 19.3 `constexpr`、`noexcept` 和 `[[nodiscard]]`

```cpp
template <typename Enum>
[[nodiscard]] constexpr auto RHIEnumToUnderlying(Enum value) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(value);
}
```

- `template`、`typename`、`constexpr`、`auto`、`noexcept`、`return` 和 `static_cast` 是关键字。
- `[[nodiscard]]` 是属性。
- `Enum`、`value` 和 `RHIEnumToUnderlying` 是普通标识符。
- `std::underlying_type_t` 是标准库类型 trait 的别名模板。

## 20. 常见选择建议

| 场景 | 推荐选择 | 说明 |
|---|---|---|
| 固定宽度整数 | `std::uint32_t` 等 | 不依赖 `int`、`long` 的平台宽度 |
| 空指针 | `nullptr` | 避免 `0`/`NULL` 的重载歧义 |
| 类型别名 | `using` | 支持别名模板且更易读 |
| 枚举 | `enum class` | 作用域明确且避免隐式整数转换 |
| 编译期常量 | `constexpr` | 同时表达常量和编译期能力 |
| 必须编译期调用 | `consteval` | 比 `constexpr` 更严格 |
| 普通安全转换 | `static_cast` | 意图明确，检查强于 C 风格转换 |
| 多态向下转换 | `dynamic_cast` | 有运行期检查 |
| 独占动态对象 | `std::unique_ptr` | 避免手写 `new`/`delete` |
| 模板能力约束 | `concept`/`requires` | 错误更接近调用位置 |
| 覆盖虚函数 | `override` | 让编译器检查签名 |
| 不抛异常接口 | `noexcept` | 明确契约，但必须保证异常不逃出 |

理解关键字时，重点不是单独记忆单词，而是确认它在当前语法位置影响的是类型、对象生命周期、编译期约束、链接方式还是控制流。相同关键字在不同位置可能具有不同含义，例如 `static` 和 `delete`，阅读时必须结合完整声明判断。
