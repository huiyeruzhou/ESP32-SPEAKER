# ESP-IDF FreeRTOS(SMP)

[TOC]

> 本文假设读者有对Vanilla FreeRTOS有足够的认识（特性，行为和API使用）

这篇文章描述了ESP-IDF和Vanilla FreeRTOS之间API和行为的区别，用于支持对称多处理（SMP）方面。

## Overview 概览

原本的FreeRTOS（这里和以后都指代Vanilla FreeRTOS）是一个小而高效的实时操作系统，支持在很多单核MCU和SoC上运行。但是，很多ESP目标（如ESP32和ESP32-S3）都支持双核对称多处理（SMP）。因此ESP-IDF中使用的FreeRTOS是一个v10.4.3版本Vanilla FreeRTOS的修改版，这些修订使得ESP-IDF FreeRTOS可以使用ESP SoC的双核SMP能力。

<!--关于加入到ESP-IDF FreeRTOS中的特性相关信息，可以查看以下链接-->

[ESP-IDF FreeRTOS Additions](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_additions.html)

<!--关于细节的ESP-IDF FreeRTOS API指南，可查看以下链接-->

[FreeRTOS API reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html)

## Symmetric Multiprocessing 对称多处理

### 基本概念

对称多处理是一个计算框架，当两个或以上相同的CPU（核）连接到一块共享的内存并且被单一操作系统所控制。

总体来说，一个SMP系统就是：

* 有多核独立运行。每个核有自己的寄存器文件，中断和中断处理；
* 内存对于每个核的显示是完全一致的。因此，访问内存某个地址的代码无论跑在哪个核上都有相同的效果。

SMP系统相对单核或者非对称处理ASMP系统的优势在于：

* 多核的出现支持多个硬件线程，因此提高了整体的处理量；
* 拥有对称式的内存是的线程可以在执行时切换核心，一般来说这可以提稿CPU的利用率。

尽管一个SMP系统允许线程切换核心，但仍让有很多情况下一个线程应当/必须运行在一个制定的核心上。因此，SMP系统中的线程会有一个核心亲和性来确定线程允许被抛在哪个确定的核心上。

* 被指定在某个核心上的线程将只能在该核心上运行
* 与被指定的核心的线程相比，没有被指定核心的线程可以在执行器件切换核心

###  在ESP目标上的SMP

ESP目标（如ESP32，ESP32-S3）都是双核SMP SoC，这些目标有着一下硬件特性使他们具有SMP能力：

* 两个完全相同的核心如CPU0（如协议CPU(Protocol CPU)或PRO_CPU）和CPU1（如应用CPU(Application CPU或APP_CPU)）.这意味着无论在哪个核心上代码执行都是完全相同的。
* 对称内存（有一些小的例外、异常）
  * 如果多核访问相同内存地址，他们的访问会在内存总线级别被序列化；
  * 对同一内存地址的真正的原子访问是通过ISA提供的原子比较和交换指令实现的。PS: ISA(Instruction Set Architecture)指令集结构
  * 跨核心的中断允许一个CPU来触发并在另一个CPU实现中断，这使得核心之间和可以通信。

[^Note]: PRO_CPU和APP_CPU是CPU0和CPU1在ESP-IDF中的别名，它反映了典型的IDF应用是如何使用两个CPU的。一般来说，处理无线网络的任务（如WiFi或蓝牙）会被指定在CPU0上（因此称为PRO_CPU），而处理剩下应用的任务就会被指定在CPU1上（因此称为APP_CPU）.

## 任务

### 创建

Vanilla FreeRTOS提供以下函数来创建任务：

* xTaskCreate()创建一个任务，任务的内存被动态分配
* xTaskCreateStatic()创建一个任务，任务的内存被静态分配（如被用户指定）

但是，在SMP系统中，任务需要被指定一个一个具体的亲和度。因此ESP-IDF提供一个PinnedToCore版本的Vanilla FreeRTOS任务创建函数：

* xTaskCreatePinnedToCore()使用一个具体的核亲和度来创建任务。任务的内存被动态分配
* xTaskCreateStaticPinnedToCore()使用一个具体的核亲和度来创建任务。任务的内存被静态分配（如用户提供）

PinnedToCore版本的任务创建函数API区别于vanilla对应函数，通过使用一个额外的xCoreID参数来指定创建的任务的核亲和度。核亲和度的有效参数为：

* 0指定创建的任务到CPU0
* 1指定创建的任务到CPU1
* tskNO_AFFINITY允许任务被运行在两个CPU上

注意ESP-IDF FreeRTOS仍然支持vanilla版本的任务创建函数，但是他们被修改为简单地使用tskNO_AFFINITY参数来调用PinnedToCore版本。（意思就是tskNO_AFFINITY参数和原本vanilla不指定核一个效果，IDF支持核调度）

[^Note]: ESP-IDF FreeRTOS在任务创建函数中同时修改了ulStackDepth的单位。任务堆大小在Vanilla FreeRTOS中被定义于字数（number of words），而在ESP-IDF FreeRTOS中，任务堆被定义为字节数（bytes）。

### 实现

任务的结构在ESP-IDF中和Vanilla中是一样的。更具体来说，ESP-IDF FreeRTOS的任务：

* 只能处于以下状态之一：运行中，准备中阻塞或暂停中；
* 任务函数通常像是一个无尽循环过程来使用
* 任务函数不应该返回

### 删除

任务删除在Vanilla中定义为vTaskDelete()函数。函数允许删除另一个任务或者当前运行的任务（如果指定的任务句柄是NULL）。实际解放的任务的内存通常被分配给闲置任务（如果被删除的任务是当前正在运行的任务）。

ESP-IDF提供相同的vTaskDelete()函数，但是，因为双核的性质，在ESP-IDF FreeRTOS中调用vTaskDelete()有一些行为上的差别：

* 当删除的任务是被指定于另一个核心上的时候，任务的内存总是被另一个核心上的空闲任务释放（由于需要清楚FPU浮点寄存器）

* 当删除的任务是另一个核心上正在运行的任务时，另一个核心上就会触发一个让步，并且该任务的内存会被其中的一个空闲任务所释放（取决于任务的核亲和度参数）
* 一个被删除的任务的内存被立即释放，当且仅当
  * 该任务运行于被指定的核上
  * 任务没有运行且没有被指定在任一核上

用户应该避免使用vTaskDelete()来删除另一个核上正在运行的任务。这是因为不知道另一个核心上正在运行着什么任务，因此可能导致无法预测的行为如：

* 删除一个正在使用锁的任务
* 删除一个还没有释放先前分配的内存的任务

尽可能地，用户应该对应用有所涉及，这样vTaskDelete()就可以使用于已知状态的任务上。如：

* 任务自删除（通过vTaskDelete(NULL)）当他们执行完成且已经完成所有已使用的资源的清理时
* 任务在被另一个任务删除前，将自己切换到暂停中状态时（通过vTaskSuspend()）

## SMP调度器

Vanilla FreeRTOS调度器是最好的，他是一种“带有时间切分的固定优先级抢占式调度器（Fixed Priority Preemptive scheduler with Time Slicing）”：

* 每一个任务在创建时会给定一个固定优先级。调度器执行最高优先级准备状态的任务
* 调度器可以切换执行另一个任务，不需要和当前正在运行的任务协作
* 调度器会定期在相同优先级的准备状态的任务之间切换（以循环方式进行），时间片是由tick中断来管理的。

ESP-IDF FreeRTOS调度器支持相同的调度特性（如固定优先级，抢占和时间片）尽管有一些细小的行为区别。

### 固定优先级

Vanilla FreeRTOS中，当调度器选择一个新的任务运行时，他会先选择当前最高优先级准备状态的任务。在ESP-IDF FreeRTOS中，每一个核都会独立调度任务的运行。当一个具体的核心选择了一个任务时，这个核心会选择可以被他运行的最高优先级的准备状态的任务。当满足以下条件时一个任务可以被该核心运行：

* 该任务有一个兼容的亲和度（如：指定该核或者没有指定任意核）
* 任务没有被另一个核心运行

但是用户不应该假设两个最高优先级的准备状态的任务总是会被调度器运行，因为同时需要考虑到核亲和度。例如：

* 任务A优先级10，指定于CPU0
* 任务B优先级9，指定于CPU0
* 任务C优先级8，指定于CPU1

结果就是调度器会选择A执行在CPU0，C执行在CPU1上，任务B尽管有着第二高的优先级但是不会被执行。

### 抢占

在Vanilla FreeRTOS中，当有一个更高优先级任务变为准备状态时，调度器可以抢占当前运行的任务去执行它。同样的在ESP-IDF中，每一个核可以被单独被调度器抢占，只要调度器确定更高优先级的任务可以运行在当前的核心上时。当多个内核可以被抢占时，调度器总是优先考虑当前的内核。换句话说，当有一个没有指定CPU的准备状态的任务，且比两个核上任务更高优先级时，调度器总是会选择抢占当前的核心。如以下场景：

* 任务A运行在CPU0，优先级8
* 任务B运行在CPU1，优先级9
* 任务C没有指定核心，优先级10，且被任务B取消阻塞时？

考虑到调度器总是优先考虑当前的核心，结果是任务A在CPU0上运行，任务C抢占了任务B。

### 时间片

Vanilla FreeRTOS调度器使用时间片意味着如果当前最高准备优先级有着多个准备状态任务时，调度器会循环地周期性地在这些任务键切换。

但是，在ESP-IDF中，没有办法这么完美地实现循环时间片，因为某个任务可能没办法运行在某个核上：

* 任务被指定在了另一个核上
* 对于没有指定的任务，该任务已经被另一个核运行

因此，当一个核心搜寻了所有准备状态的任务表来选择一个任务执行时，核心可能跳过一些相同优先级的任务或选择了一个更低优先级的它可以运行的准备状态的任务。

ESP-IDF FreeRTOS调度器为相同优先级的就绪状态任务实现了最佳努力（Best Effort）Round Robin时间切分，确保被选中运行的任务将被放在列表的后面，从而使未被选中的任务在下一次调度迭代（即下一次tick中断或产量）中拥有更高的优先权。

以下例子解释了Best Effort Round Robin时间切分，假设：

* 当前四个相同优先级就绪状态任务AX, B0, C1, D1，其中优先级为当前就绪状态任务中的最高优先级，第一个字符代表任务的名称（如A, B, C, D）- 第二个字符表示任务的指定核心（X表示未指定）
* 任务列表总是从头搜索

```
-----------------------------------------------------------------------------
1. 开始状态，没有任何一个就绪状态任务被选择去执行
Head [ AX, B0, C1, D0 ] Tail
-----------------------------------------------------------------------------
2. 核心0有一个tick中断并且搜索一个任务去运行。
任务A被选择企鹅杯移动了到了列表末尾

Core0-- |
Head [  AX, B0, C1, D0 ] Tail

					0
Head [  B0, C1, D0, AX ] Tail
-----------------------------------------------------------------------------
3. 核心1有一个tick中断并且搜索一个任务去运行。
任务B无法被运行因为核亲和度不匹配，所以核1跳过到任务C。
任务C被选择并被移动到列表末尾

Core1 ---- |	   0
Head [ B0, C1, D0, AX ] Tail

               0   1
Head [ B0, D0, AX, C1 ] Tail
-----------------------------------------------------------------------------
4. 核心0有另一个tick中断并且搜索一个任务去运行。
任务B被选中并被移动到李彪末尾

Core0- |	   0   1
Head [ B0, D0, AX, C1 ] Tail

           0   1   0
Head [ D0, AX, C1, B0 ] Tail
-----------------------------------------------------------------------------
5. 核心1有另一个tick中断并且搜索一个任务去运行。
任务D无法被运行，因为核亲和度不匹配，所以核心1跳到任务A
任务A被选择并被移动到列表末尾

Core1 ---- |   1   0
Head [ D0, AX, C1, B0 ] Tail

		   1   0   1
Head [ D0, C1, B0, AX ] Tail
-----------------------------------------------------------------------------
```

对于用户来说，Best Effort Round Robin时间切片的影响：

* 用户无法预测到多个相同优先级就绪状态任务运行顺序（像Vanilla FreeRTOS一样的顺序），像上述例子一样，核心会跳过一些任务
* 但是，给定足够的tick，任务终会被给予一定的处理时间
* 如果一个核心无法在最高就绪状态优先级找一个可以运行的任务，他会降低一个优先级来搜索任务
* 为了实现理想的round robin时间片，用户需要确保所有确定的优先级的任务被指定到同个核心上

### Tick中断

Vanilla FreeRTOS需要一个定期的tick中断发生，tick中断负责：

* 增加调度器的tick计数
* 解锁或者锁定超时的任务
* 检查是否是否需要时间片（如触发一个上下文切换）
* 执行一个应用tick钩子

在ESP-IDF中，每一个核心都会收到一个周期性的中断和独立运行tick中断。每个核心上的tick中断都是相同间隔的但是可以不在一个阶段。

但是，上述tick职责并不被所有核心运行：

* CPU0执行以上所有tick中断职责
* CPU1只检查时间片和执行应用tick钩子

[^Note]: CPU0在ESP-IDF中单独负责保持时间，因此任何阻止CPU0增加tick计数（如暂停CPU0的调度器）的时间都会导致整个调度器时间保持滞后。

### 空闲任务

Vanilla FreeRTOS在启动调度程序时将隐形的创建一个优先级为0的空闲任务。当没有其他任务准备运行时，空闲任务就会运行，它有以下职责：

* 释放被删除的任务的内存
* 执行应用空闲钩子

在ESP-IDF FreeRTOS中，每个核都会创建一个单独指定的空闲任务，每个核心上的空闲任务有着和Vanilla相同的职责。

### 调度器暂停

Vanilla FreeRTOS云讯调度器被暂停/继续通过调用vTaskSuspendAll()和xTaskResumeAll()。当调度器被暂停时：

* 任务切换被禁止，但是中断使能
* 禁止调用任何阻塞/让步函数，并且禁用时间切分。
* tick计数被冻结（但是tick中断仍然发生来执行应用tick钩子）

在调度器恢复时，xTaskRersumeAll()会将赶上所有损失的时间，并解除任何超时的任务。

在ESP-IDF FreeRTOS中，暂停跨多核的调度器是不可能的，因此当调用vTaskSuspendAll()是针对某一个特定的核心的（如A）：

* 任务切换只在核A上禁止，但是A核的中断仍然使能
* 调用任何阻塞或让步函数在A核心上禁止，时间切片在A核心上禁止
* 如果A核心上的中断解除任何任务，这些任务会前往A核心自己的暂停就绪任务列表
* 如果A核心是CPU0，tick计数被冻结且暂停tick计数会增加。但是tick中断仍会发生来执行程序tick钩子

当xTaskResumeAll()被某个核心（如A）调用时：

* 任何添加到核心A的暂停就绪任务列表的任务会被继续
* 如果核心A是CPU0，暂停的tick计数就会被解开来追上丢失的tick

[^Warning]: 因为在ESP-IDF的调度器暂停只暂停某个具体的核心的调度器，调度器暂停不是一个有效的方法来确保在访问共享数据时任务之间相互排斥。用户应当使用正确的锁源语来保证任务互斥，如互斥锁或自旋锁。

### 禁止中断

Vanilla允许调用taskDISABLE_INTERRUPTS和taskENABLE_INTERRUPTS来禁止或使能中断。

ESP-IDF提供相同的API，但是中断的禁止和使能只会作用当前核心。

[^Warning]: 禁止中断在Vanilla中是一个有效的方法来实现互斥（和在单核系统中）。但是在SMP系统中，禁止中断并不是有效的方法来保证互斥，参考关键节来获取更多细节。

### 启动和结束

ESP-IDF不需要用户调用vTaskStartScheduler()来启动调度器，ESP-IDF应用的启动流程会自动调用这个函数。每个用户代码的进入点是一个用户定义的void app_main(void)函数。更多细节参考ESP-IDF FreeRTOS applications,查看[ESP-IDF FreeRTOS Applications](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html#freertos-applications).

ESP-IDF不支持调度器终止，调用vTaskEndScheduler()会导致应用结束。

## 临界段

### API改变

Vanilla FreeRTOS通过使用关闭中断来实现临界段，这阻止了临界段中的抢占式上下文切换和ISR（Interrupt Service Routine）的服务。因此一个进入临界段的任务或者中断服务程序ISR被保证为访问共享资源的唯一实体。在Vanilla FreeRTOS中，临界区有以下API：

* taskENTER_CRITICAL() 通过禁用中断来进入一个临界区
* taskEXIT_CRITICAL()通过重新使能中断来退出一个临界区
* taskENTER_CRITICAL_FROM_ISR()通过禁用中断嵌套来进入一个临界区
* taskEXIT_CRITICAL_FROM_ISR() 通过重新使能中断嵌套来退出一个临界区

但是，在一个SMP系统中，紧紧关闭中断并不能形成临界区，其他核心的存在意味着共享资源仍然可以被并发放稳。因此，ESP-IDF中的临界区使用自旋锁来实现。为了适应自旋锁，ESP-IDf  FreeRTOS的临界区API包含以下额外的自旋锁参数：

* 自旋锁包含于portMUX_TYPE中（不和FreeRTOS的mutexes混淆）
* taskENTER_CRITICAL(&mux)从任务上下文进入一个临界区
* taskEXIT_CRITICAL(&mux)从任务上下文离开一个临界区
* taskENTER_CRITICAL_ISR(&mux)从中断上下文进入一个临界区
* taskEXIT_CRITICAL_ISR(&mux)从中断上下文离开一个临界区

[^Note]: 临界区API可以被递归调用（如嵌套临界区），多次递归进入临界区是有效的，只要临界区同时退出了相同的次数。但是，临界区针对于不同的自旋锁，用户应该避免在递归进入临界区时的死锁。

### 使用

在ESP-IDF中，某个核心进入和离开临界区的步骤如下：

* 对于taskENTER_CRITICAL(&mux)(或taskENTER_CRITICAL_ISR(&mux))：

  1. 核心依靠configMAX_SYSCALL_INTERRUPT_PRIORITY禁止中断（或者嵌套中断）

  2. 该核心使用原子比较和设置指令在自旋锁上旋转，直到获得锁。当核心可以设置锁所属值为核心ID时，就获得了一个锁。
  3. 一旦获得了自旋锁，函数返回。临界区剩下的部分运行时禁止中断（或嵌套中断）

* 对于taskEXIT_CRITICAL(&mux)（或taskEXIT_CRITICAL_ISR(&mux)）

  	1. 核心通过清除自旋锁所属值来释放自旋锁
  	1. 核心重新使能中断（或中断嵌套）

### 限制和考虑

鉴于临界区中中断（或者嵌套中断）被禁止，对于在临界区可以做什么有很多限制。在临界区中，用户需要记住一下限制和考虑：

* 临界区应当被尽量短保留：
  * 临界区持续时间越长，暂停中断的滞后就越长
  * 一个典型的临界区应该只访问少量的数据结构和/或硬件寄存器（与或硬件寄存器？）
  * 如果可能，将尽可能多的处理与或时间推迟到临界区外处理。
* FreeRTOS API不应该在临界区中被调用
* 用户不应该在临界区中调用任何阻塞或者让步函数

## 杂项

### 浮点使用

通常情况下，当上下文切换发生时：

* CPU寄存器的当前状态被存到正在切换的任务的堆栈中
* 之前被存放的CPU寄存器状态被从将要切换到的任务的堆栈中加载

但是，ESP-IDF为CPU的FPU浮点单元寄存器实现了懒惰上文切换方式。换句话说，当某个核心上发生了上下文切换时（如CPU0），核心的FPU寄存器状态不会被立即存储到正在切换的任务的堆栈中（如TaskA），FPU的寄存器将不会被触及直到：

* 一个不同的任务（如TaskB）运行在同一个核心上并且要使用FPU，这会触发一个异常来保存FPU寄存器到任务A的堆栈中
* 任务A被调度到用一个核心上并且继续执行，这种情况下不需要保存或者还原FPU寄存器。

但是，鉴于任务可以是未指定核的，因此可以被调度到不同的核心上（如TaskA调度到CPU1上），跨核拷贝和恢复FPU寄存器是行不通的。因此，当一个任务使用FPU（通过使用一个float类型在调用过程中时），ESP-IDF会自动将该任务指定pin到当前运行的核上。这保证了所有使用FPU的任务总是被指定到某个CPU的。

不仅如此，ESP-IDF默认不支持在中断上下文中使用FPU，因为FPU寄存器状态和特定任务绑定的。

[^Note]: 包含FPU在内的ESP目标不支持对双精度浮点运算的硬件加速（double）。相反，double是通过软件实现的，因此关于float的行为限制并不适用于double。注意到运维缺乏硬件加速，双精度double相比单精度float的运算可能会消耗大量的CPU时间。

###  ESP-IDF FreeRTOS单核

尽管ESP-IDf FreeRTOS是一个SMP调度器，一些ESP目标是单核的（如ESP32-S2和ESP32-C3），当为这些目标构建ESP-IDF应用时，ESP-IDF仍会被使用但是核数会被设置为1（如CONFIG_FREERTOS_UNICORE在单核目标下总会被使能）。

对于多核目标（如ESP32和ESP32-S3），CONFIG_FREERTOS_UNICORE也可以被设置。这会导致ESP-IDF FreeRTOS只运行CPU0，其他核心不被启动。

[^Note]: 用户需要谨记在心，使能CONFIG_FREERTOS_UNICORE不等于运行Vanilla FreeRTOS。ESP-IDF FreeRTOS的额外的API仍然可以被调用，造成的行为变化编译给单核目标时仍会产生少量开销。
