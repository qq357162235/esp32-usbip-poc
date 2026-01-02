# ESP-IDF 组件依赖：REQUIRES vs PRIV_REQUIRES

在 ESP-IDF 中，`idf_component_register` 函数用于注册组件，其中有两种声明依赖的方式：

## REQUIRES
- **依赖传递性**：公共依赖，会传递给使用当前组件的其他组件
- **可见性**：被依赖组件的头文件对当前组件及其使用者都可见
- **使用场景**：当当前组件的公共 API 使用了被依赖组件的类型或函数时

## PRIV_REQUIRES
- **依赖传递性**：私有依赖，不会传递给使用当前组件的其他组件
- **可见性**：被依赖组件的头文件仅对当前组件内部可见
- **使用场景**：当当前组件仅在内部实现中使用被依赖组件，而公共 API 不暴露其细节时

## 示例

```cmake
# 组件 A 的 CMakeLists.txt
idf_component_register(
    SRCS "a.c"
    INCLUDE_DIRS "include"
    REQUIRES component_b      # 公共依赖
    PRIV_REQUIRES component_c # 私有依赖
)

# 组件 X 依赖组件 A
idf_component_register(
    SRCS "x.c"
    INCLUDE_DIRS "include"
    REQUIRES component_a
)
```

在这个例子中：
- 组件 X 会自动获得对 component_b 的依赖（因为 component_a 的 REQUIRES 会传递）
- 组件 X 不会获得对 component_c 的依赖（因为 component_a 的 PRIV_REQUIRES 不传递）

## 最佳实践
- 将仅内部使用的依赖声明为 PRIV_REQUIRES，减少依赖传递
- 将公共 API 依赖的组件声明为 REQUIRES，确保依赖传递
- 合理使用依赖类型可以降低组件间的耦合度，提高代码可维护性