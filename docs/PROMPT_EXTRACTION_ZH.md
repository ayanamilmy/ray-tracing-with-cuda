请从我的Windows完整绝区零客户端提取角色渲染数据。先确认安装路径、客户端版本和目标角色/皮肤；只读客户端，另建输出目录，优先读本地文件，不必启动游戏或重下几十GB。先读渲染器仓库的WINDOWS_HANDOFF.md；以前Mac临时解包脚本未全部上传，不能假装本机已有。

沿用之前的方法：Manifest与资源索引→角色配置哈希→入口bundle→递归依赖。检查ZenlessZoneZero_Data/StreamingAssets/Blocks；1677012929.blk只是历史索引线索，不保证当前同名。记录版本、块名、偏移、对象ID和校验；旧bundle编号、哈希及混淆字段不能直接套用。身份须读实际对象确认。

参考已成功使用的AnimeStudio Ultimate格式代码，先确认支持当前版本。资源可能包含mhy封装、Ooz/Kraken或LZ4、Unity序列化、外部资源流及ACL动画。普通Unity导出器未必支持；Mac的.dylib须换兼容DLL或移植源码。先验证一个对象，再扩大提取。

叶瞬光内部名Zhenzhen、角色1431；蕾米Remielle为1581，泳装3115812，标记RemiellePasSeul/PasSeul。三页目标为Detail、Skill_01、Equipment及DtoS1、S1toE、EtoD。解析控制器，补齐身体、Default、泳装附件及表情层，不能只播服装动作。

保留LOD0网格、骨架、绑定矩阵、权重、UV、切线、顶点色、表情和原始材质贴图引用，检查武器、附件与运行时路径依赖。OBJ只作检查，另存原始对象和NPZ/JSON；单骨骼网格可能没有权重通道。

相机既查ConfigUIAvatarShow，也查独立UIAvatarShowSettings对象与公共曲线库；共用系统不等于参数一致。Default_ClampAuto曾读到两端零斜率，可推得3t²−2t³，但时间输入、坐标系和旋转顺序仍未核验。IL2CPP元数据可能是MHY格式，别硬套旧工具。

交付可求值动作、材质绑定、相机原字段和定位证据，先做少量预览验证。不要编造官方参数；说明哪些已确认、哪些是项目推断。
