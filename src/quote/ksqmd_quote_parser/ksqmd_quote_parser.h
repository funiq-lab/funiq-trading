#ifndef KSQMD_QUOTE_PARSER_H
#define KSQMD_QUOTE_PARSER_H

#include "quote/base_quote_parser.h"
#include "types/quote_types.h"
#include "KsHSInterfaceEx.h"
#include "KsHSDef.h"
#include <string>
#include <memory>

// 前向声明 - KSQMD SDK相关结构体
namespace istone_hs {
    struct MetaInfo;        ///< KSQMD元信息结构体
    struct AuthInfo;        ///< KSQMD认证信息结构体
    struct CacheAdvanceNicItem;   ///< 网卡配置项结构体
    struct CacheAdvanceQueueItem; ///< 队列配置项结构体
}

namespace istone {
    namespace comm {
        enum STATUS;        ///< KSQMD状态枚举
    }
}

class XMLConfigEx;          ///< XML配置解析器类

NS_FT_BEGIN

/**
 * KSQMD行情解析器实现类
 * 
 * 基于KSQMD（金仕达高速行情数据接口）实现的行情解析器，专门用于接收和解析
 * 沪深交易所的Level-2高速行情数据。继承自BaseQuoteParser，只需实现KSQMD
 * 特定的连接和数据解析功能，通用功能由基类提供。
 * 
 * **KSQMD特性**：
 * - 高性能：基于UDP组播的高速数据传输
 * - 低延迟：微秒级的数据接收延迟
 * - 全市场：支持沪深两市所有证券品种
 * - 多数据类型：指数、快照、50档、逐笔委托、逐笔成交
 * - 多队列：支持多个数据队列并行接收
 * - 网卡绑定：支持绑定特定网卡接收数据
 * 
 * **支持的数据类型**：
 * 1. **指数行情**：上证指数、深证指数等各类指数实时数据
 * 2. **快照行情**：Level-1行情数据，包含买卖五档
 * 3. **50档行情**：Level-2深度行情，包含买卖50档队列
 * 4. **逐笔委托**：实时委托数据，反映市场委托意向
 * 5. **逐笔成交**：实时成交数据，反映实际交易情况
 * 
 * **配置要求**：
 * - 需要KSQMD的XML配置文件
 * - 需要有效的KSQMD授权许可
 * - 需要配置正确的网络参数
 * - 需要绑定到正确的网卡接口
 * 
 * **性能特点**：
 * - 支持多线程并行处理
 * - 零拷贝数据转换
 * - 高效的内存管理
 * - 实时性能监控
 * 
 * **使用场景**：
 * - 高频交易系统
 * - 实时行情分发
 * - 量化交易平台
 * - 行情数据存储
 */
class KsqmdQuoteParser : public BaseQuoteParser {
public:
    /**
     * 构造函数
     * 
     * 初始化KSQMD解析器实例，设置默认状态。
     * 不进行实际的初始化工作，具体初始化在init_impl()中完成。
     */
    KsqmdQuoteParser();
    
    /**
     * 析构函数
     * 
     * 清理KSQMD相关资源，确保SDK正确释放。
     * 会自动调用release_impl()进行清理。
     */
    virtual ~KsqmdQuoteParser();

    // 禁用拷贝构造和赋值操作
    KsqmdQuoteParser(const KsqmdQuoteParser&) = delete;
    KsqmdQuoteParser& operator=(const KsqmdQuoteParser&) = delete;

protected:
    // ===== 实现基类的核心虚函数 =====
    
    /**
     * KSQMD特定的初始化实现
     * 
     * 执行KSQMD SDK的初始化流程：
     * 1. 解析XML配置文件
     * 2. 初始化元信息和认证信息
     * 3. 配置网卡和队列参数
     * 4. 注册数据接收回调函数
     * 5. 验证授权许可
     * 
     * @param config 行情配置信息，包含KSQMD配置文件路径等
     * @return 0成功，负数失败
     * 
     * @note 需要有效的KSQMD配置文件和授权许可
     * @note 会验证网络配置的有效性
     */
    int init_impl(const QuoteConfig* config) override;
    
    /**
     * KSQMD特定的启动实现
     * 
     * 启动KSQMD数据接收服务：
     * 1. 启动内部处理线程
     * 2. 初始化数据接收队列
     * 3. 准备接收行情数据
     * 
     * @return 0成功，负数失败
     * 
     * @note 不会立即开始接收数据，需要调用connect_impl()
     */
    int start_impl() override;
    
    /**
     * KSQMD特定的连接实现
     * 
     * 建立KSQMD数据连接：
     * 1. 绑定到指定的网卡接口
     * 2. 加入组播组接收数据
     * 3. 启动数据接收循环
     * 4. 执行认证流程
     * 
     * @return 0成功，负数失败
     * 
     * @note 需要正确的网络配置和授权
     * @note 连接成功后开始接收实时行情数据
     */
    int connect_impl() override;
    
    /**
     * KSQMD特定的断开实现
     * 
     * 断开KSQMD数据连接：
     * 1. 停止数据接收循环
     * 2. 退出组播组
     * 3. 解绑网卡接口
     * 4. 清理连接状态
     * 
     * @return 0成功，负数失败
     * 
     * @note 断开后可以重新连接
     * @note 不会清理配置信息
     */
    int disconnect_impl() override;
    
    /**
     * KSQMD特定的停止实现
     * 
     * 停止KSQMD服务：
     * 1. 停止所有处理线程
     * 2. 清理数据接收队列
     * 3. 重置内部状态
     * 
     * @return 0成功，负数失败
     * 
     * @note 停止后需要重新启动才能使用
     */
    int stop_impl() override;
    
    /**
     * KSQMD特定的清理实现
     * 
     * 清理所有KSQMD相关资源：
     * 1. 释放SDK资源
     * 2. 清理配置对象
     * 3. 释放内存缓冲区
     * 4. 重置所有状态
     * 
     * @note 清理后需要重新初始化才能使用
     */
    void release_impl() override;

private:
    // ===== KSQMD特定的私有成员 =====
    XMLConfigEx* _xml_config;          ///< XML配置解析器实例
    std::string _config_file;          ///< 配置文件路径
    
    // KSQMD SDK配置结构体
    std::unique_ptr<istone_hs::MetaInfo> _meta_info;     ///< 元信息配置
    std::unique_ptr<istone_hs::AuthInfo> _auth_info;     ///< 认证信息配置
    std::unique_ptr<istone_hs::CacheAdvanceNicItem[]> _nic_list;   ///< 网卡配置列表
    std::unique_ptr<istone_hs::CacheAdvanceQueueItem[]> _queue_list; ///< 队列配置列表
    uint8_t _nic_count;                 ///< 网卡数量
    uint8_t _queue_count;               ///< 队列数量

    // ===== KSQMD回调函数（静态方法）=====
    
    /**
     * 数据接收回调函数
     * 
     * KSQMD SDK在接收到行情数据时调用此函数。
     * 
     * @param data 行情数据指针
     * @param queue_id 数据队列ID
     * 
     * @note 这是静态回调函数，在KSQMD的接收线程中执行
     * @note 需要快速处理，避免阻塞数据接收
     * @note 通过全局实例指针转发到实例方法
     */
    static void _data_receiver_callback(void* data, uint32_t queue_id);
    
    /**
     * 认证状态回调函数
     * 
     * KSQMD SDK在认证状态发生变化时调用此函数。
     * 
     * @param status 认证状态
     * @param msg 状态消息
     * 
     * @note 这是静态回调函数，用于监控认证状态
     * @note 认证失败时会影响数据接收
     */
    static void _auth_status_callback(istone::comm::STATUS status, const char* msg);
    
    // ===== 数据处理私有方法 =====
    
    /**
     * 处理接收到的行情数据
     * 
     * 解析KSQMD原始数据，根据数据类型分发到相应的处理函数。
     * 
     * @param data 行情数据指针
     * @param queue_id 数据队列ID
     * 
     * @note 在数据接收线程中执行，需要高效处理
     * @note 会根据消息头判断数据类型和交易所
     */
    void _process_data(void* data, uint32_t queue_id);
    
    /**
     * 解析上交所行情数据
     * 
     * 根据消息类型解析上交所的各种行情数据。
     * 
     * @param msg_type 消息类型标识
     * @param data 数据指针
     * 
     * @note 支持指数、快照、50档、逐笔委托、逐笔成交等数据类型
     */
    void _parse_sse_data(char msg_type, void* data);
    
    /**
     * 解析深交所行情数据
     * 
     * 根据消息类型解析深交所的各种行情数据。
     * 
     * @param msg_type 消息类型标识
     * @param data 数据指针
     * 
     * @note 支持指数、快照、50档、逐笔委托、逐笔成交等数据类型
     */
    void _parse_szse_data(char msg_type, void* data);
    
    // ===== 数据转换方法 =====
    
    /**
     * 转换上交所指数数据
     * @param sse_index 上交所指数数据指针
     * @return 标准化的指数数据结构
     * @note 将KSQMD的上交所指数格式转换为系统标准格式
     */
    IndexData _convert_sse_index(const void* sse_index);
    
    /**
     * 转换深交所指数数据
     * @param szse_index 深交所指数数据指针
     * @return 标准化的指数数据结构
     * @note 将KSQMD的深交所指数格式转换为系统标准格式
     */
    IndexData _convert_szse_index(const void* szse_index);
    
    /**
     * 转换上交所快照数据
     * @param sse_depth 上交所快照数据指针
     * @param has_price 是否包含价格信息
     * @return 标准化的深度数据结构
     * @note 将KSQMD的上交所快照格式转换为系统标准格式
     */
    DepthData _convert_sse_depth(const void* sse_depth, bool has_price);
    
    /**
     * 转换深交所快照数据
     * @param szse_depth 深交所快照数据指针
     * @return 标准化的深度数据结构
     * @note 将KSQMD的深交所快照格式转换为系统标准格式
     */
    DepthData _convert_szse_depth(const void* szse_depth);
    
    /**
     * 转换上交所50档数据
     * @param sse_depth_fifty 上交所50档数据指针
     * @return 标准化的50档深度数据结构
     * @note 将KSQMD的上交所50档格式转换为系统标准格式
     */
    DepthFiftyData _convert_sse_depth_fifty(const void* sse_depth_fifty);
    
    /**
     * 转换深交所50档数据
     * @param szse_depth_fifty 深交所50档数据指针
     * @return 标准化的50档深度数据结构
     * @note 将KSQMD的深交所50档格式转换为系统标准格式
     */
    DepthFiftyData _convert_szse_depth_fifty(const void* szse_depth_fifty);
    
    /**
     * 转换上交所逐笔委托数据
     * @param sse_entrust 上交所逐笔委托数据指针
     * @return 标准化的委托数据结构
     * @note 将KSQMD的上交所委托格式转换为系统标准格式
     */
    EntrustData _convert_sse_entrust(const void* sse_entrust);
    
    /**
     * 转换深交所逐笔委托数据
     * @param szse_entrust 深交所逐笔委托数据指针
     * @return 标准化的委托数据结构
     * @note 将KSQMD的深交所委托格式转换为系统标准格式
     */
    EntrustData _convert_szse_entrust(const void* szse_entrust);
    
    /**
     * 转换上交所逐笔成交数据
     * @param sse_trade 上交所逐笔成交数据指针
     * @return 标准化的成交数据结构
     * @note 将KSQMD的上交所成交格式转换为系统标准格式
     */
    TradeData _convert_sse_trade(const void* sse_trade);
    
    /**
     * 转换深交所逐笔成交数据
     * @param szse_trade 深交所逐笔成交数据指针
     * @return 标准化的成交数据结构
     * @note 将KSQMD的深交所成交格式转换为系统标准格式
     */
    TradeData _convert_szse_trade(const void* szse_trade);

    // ===== 配置和工具方法 =====
    
    /**
     * 初始化XML配置
     * 
     * 解析KSQMD的XML配置文件，提取网络配置、认证信息等。
     * 
     * @param config_file XML配置文件路径
     * @return 0成功，负数失败
     * 
     * @note 配置文件必须符合KSQMD的XML格式规范
     * @note 会验证配置的完整性和有效性
     */
    int _initialize_xml_config(const char* config_file);
    
    /**
     * 映射KSQMD状态到系统状态
     * 
     * 将KSQMD SDK的状态枚举转换为系统标准状态。
     * 
     * @param ksqmd_status KSQMD状态枚举
     * @return 系统标准状态枚举
     * 
     * @note 用于状态统一管理和回调通知
     */
    static Status _map_ksqmd_status_to_system_status(istone::comm::STATUS ksqmd_status);
    
    /**
     * 获取KSQMD状态描述字符串
     * 
     * 将KSQMD状态枚举转换为可读的字符串描述。
     * 
     * @param status KSQMD状态枚举
     * @return 状态描述字符串
     * 
     * @note 用于日志记录和错误诊断
     */
    static const char* _get_ksqmd_status_string(istone::comm::STATUS status);

    /**
     * 全局实例指针
     * 
     * 用于静态回调函数访问实例方法。
     * KSQMD SDK使用C风格的回调函数，需要通过全局指针转发到实例。
     * 
     * @note 同时只能有一个KSQMD解析器实例
     * @note 在构造函数中设置，析构函数中清空
     */
    static KsqmdQuoteParser* _instance;
};

NS_FT_END

#endif // KSQMD_QUOTE_PARSER_H

