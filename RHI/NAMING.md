# RHI 命名约定

公共类型统一采用 `RHI` 前缀，避免与应用层类型或后端实现发生冲突。
| 后缀/前缀 | 含义 | 示例 |
| --- | --- | --- |
| `RHI` | 枚举或位掩码枚举 | `RHIResourceState` |
| `RHI...Desc` | 创建或配置描述结构 | `RHITextureDesc` |
| `RHI...Handle` | 设备拥有对象的不透明强类型句柄 | `RHIBufferHandle` |
| `RHI...State` | 可直接用于管线或资源的状态结构 | `RHIDepthStencilState` |
| `RHI...Layout` | 绑定槽位或渲染目标的静态接口描述 | `RHIPipelineLayoutDesc` |
| `RHI...Binding` | 一次资源绑定的数据 | `RHIBufferBinding` |
| `RHI...Range` | 连续子范围，通常使用偏移与数量表示 | `RHISubresourceRange` |
| `RHI...Barrier` | 资源状态或内存排序屏障 | `RHITextureBarrier` |
| `RHI...Ptr` | 具有共享所有权的接口对象指针 | `RHIDevicePtr` |
| `IRHI...` | 由后端实现的纯抽象接口 | `IRHIDevice` |

字段命名遵循“对象 + 用途”的顺序：`SourceOffset`、`DestinationOffset`、`InitialState`、`FinalState`。布尔字段使用 `Is`、`Has`、`Allow`、`Enable` 或 `...Dynamic` 表示其判断或开关语义。
