请继续我的绝区零角色纯路径追踪渲染器。仓库为 ayanamilmy/ray-tracing-with-cuda，分支 codex/windows-renderer-handoff-20261008。我已换到Windows，先确认本机仓库与游戏路径，读WINDOWS_HANDOFF.md，不要沿用Mac绝对路径。

根目录CUDA和公共头文件是我手写的，禁止改写。来源关系是main_accum.cu→Metal→独立NVIDIA版，后者构建不直接依赖根目录文件。后续修改放在独立目录，动源码前检查VS Code保存状态。

metal只供Mac使用；nvidia_port包含独立CUDA、OptiX RT Core及DLSS。最新1K/64spp任务副本在variants/remielle_dlss64，三页驱动在examples/remielle_three_pages/job。常规reconstruction是Quality版，不能当作最新UltraPerformance版。构建前按说明恢复nvcc输入源码。独立后端已验证Linux RTX5090，Windows原生移植尚未完成；可从Windows通过SSH控制Linux云机，WSL2支持须实测。

叶瞬光和蕾米埃尔泳装已提取模型、贴图、骨骼、表情、三页动作和部分相机配置。原生蕾米成片3024×1964、120fps、256spp、12秒；最新1008×655输入、DLSS输出3K、64spp、1440帧，已下载。没有插帧。保留路径追踪，但AI重建会改变图像，不能承诺等同原生画质。

GitHub不含游戏资产与成片。已有92MB的remielle-three-pages-cloud.tar.gz需另外迁移，优先复用，避免重新解包。云端NVENC曾不可用，最终用CPU H.264编码，MOV另补QuickTime正常速率标记。检查非空视频、帧数、时长和帧率；异常栈可能含VIDEO_DONE，不能凭子串或SSH断线判定成功。旧实例地址和密码不能沿用。

请先汇报已有文件和缺少的数据，再按我新要求工作。官方字段、推断参数与视觉调整要区分；相机坐标转换和运行时插值尚未完全核验。未经新授权，不要租机、充值或开收费实例。
