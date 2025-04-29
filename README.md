# memalloc

**memalloc 是一个简单的内存分配器。**

它实现了 `malloc()`、`calloc()`、`realloc()` 和 `free()`。

---

## 文章

[写一个简单的内存分配器](http://8.138.192.226/)

---

## 编译与运行

使用以下命令编译生成共享对象：

```bash
gcc -o memalloc.so -fPIC -shared memalloc.c
```

`-fPIC` 和 `-shared` 选项的作用是生成位置无关代码（Position Independent Code），并告诉链接器创建适合动态链接的共享对象。

---

## 使用方法

在 Linux 上，如果你将环境变量 `LD_PRELOAD` 设置为某个共享对象（.so 文件）的路径，那么在运行任何程序前，该文件都会被优先加载。我们可以利用这个技巧来优先加载我们编写的内存分配器库，这样之后在 shell 中运行的命令就会使用我们实现的 `malloc()`、`free()`、`calloc()` 和 `realloc()`。

```bash
export LD_PRELOAD=$PWD/memalloc.so
```

例如，运行如下命令：

```bash
ls
```

输出可能类似于：

```
memalloc.c    memalloc.so    README.md
```

或者打开文件：

```bash
vim memalloc.c
```

你也可以使用这个内存分配器运行你自己的程序。

---

## 停止使用

完成实验后，可以使用以下命令取消 `LD_PRELOAD` 的设置：

```bash
unset LD_PRELOAD
```
