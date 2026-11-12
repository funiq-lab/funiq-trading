-- Migration: initial_tables
-- Created: 2024-01-09 12:00:00
-- Description: Create initial database tables for strategy pending orders and strategy orders

-- Revision identifiers, used by Migration Manager
-- revision: 604NAX397312
-- down_revision: null

-- ============================================================================
-- UPGRADE SECTION
-- ============================================================================
-- @upgrade_begin

-- 创建策略挂单表 - 添加 status 字段
CREATE TABLE IF NOT EXISTS strategy_pending_orders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    strategy_id TEXT NOT NULL,
    pending_order_info TEXT,
    status INTEGER DEFAULT 1,
    create_time INTEGER NOT NULL,
    update_time INTEGER DEFAULT 0,
    login_id TEXT
);

-- 创建策略订单表
CREATE TABLE IF NOT EXISTS strategy_orders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    pending_order_id INTEGER NOT NULL,
    strategy_id TEXT NOT NULL,
    pending_order_info TEXT,
    pending_submit_time INTEGER DEFAULT 0,
    
    -- 账户信息
    login_id TEXT,
    cust_id TEXT,
    fund_account_id TEXT,
    account_id TEXT,
    
    -- 订单核心信息
    security_code TEXT NOT NULL,
    sign INTEGER DEFAULT 2,
    order_id INTEGER DEFAULT 0,
    exchange_order_id TEXT,
    batch_id INTEGER DEFAULT 0,
    
    side INTEGER NOT NULL,
    order_type INTEGER NOT NULL,
    status INTEGER DEFAULT 0,
    status_msg TEXT,
    
    price REAL NOT NULL,
    qty INTEGER NOT NULL,
    leaves_qty INTEGER DEFAULT 0,
    cum_qty INTEGER DEFAULT 0,
    avg_price REAL DEFAULT 0.0,
    cum_amt REAL DEFAULT 0.0,
    
    order_time INTEGER DEFAULT 0,
    update_time INTEGER DEFAULT 0,
    create_time INTEGER NOT NULL,
    
    error_code INTEGER DEFAULT 0,
    error_msg TEXT,
    
    FOREIGN KEY (pending_order_id) REFERENCES strategy_pending_orders(id) ON DELETE CASCADE
);

-- 创建策略挂单表索引
CREATE INDEX IF NOT EXISTS idx_strategy_pending_orders_strategy_id ON strategy_pending_orders(strategy_id);
CREATE INDEX IF NOT EXISTS idx_strategy_pending_orders_status ON strategy_pending_orders(status);
CREATE INDEX IF NOT EXISTS idx_strategy_pending_orders_create_time ON strategy_pending_orders(create_time);
CREATE INDEX IF NOT EXISTS idx_strategy_pending_orders_login_id ON strategy_pending_orders(login_id);

-- 创建策略订单表索引
CREATE INDEX IF NOT EXISTS idx_strategy_orders_pending_order_id ON strategy_orders(pending_order_id);
CREATE INDEX IF NOT EXISTS idx_strategy_orders_strategy_id ON strategy_orders(strategy_id);
CREATE INDEX IF NOT EXISTS idx_strategy_orders_order_id ON strategy_orders(order_id);
CREATE INDEX IF NOT EXISTS idx_strategy_orders_security_code ON strategy_orders(security_code);
CREATE INDEX IF NOT EXISTS idx_strategy_orders_status ON strategy_orders(status);
CREATE INDEX IF NOT EXISTS idx_strategy_orders_create_time ON strategy_orders(create_time);
CREATE INDEX IF NOT EXISTS idx_strategy_orders_login_id ON strategy_orders(login_id);

-- @upgrade_end

-- ============================================================================
-- DOWNGRADE SECTION  
-- ============================================================================
-- @downgrade_begin

-- 删除索引
DROP INDEX IF EXISTS idx_strategy_pending_orders_strategy_id;
DROP INDEX IF EXISTS idx_strategy_pending_orders_status;
DROP INDEX IF EXISTS idx_strategy_pending_orders_create_time;
DROP INDEX IF EXISTS idx_strategy_pending_orders_login_id;

DROP INDEX IF EXISTS idx_strategy_orders_pending_order_id;
DROP INDEX IF EXISTS idx_strategy_orders_strategy_id;
DROP INDEX IF EXISTS idx_strategy_orders_order_id;
DROP INDEX IF EXISTS idx_strategy_orders_security_code;
DROP INDEX IF EXISTS idx_strategy_orders_status;
DROP INDEX IF EXISTS idx_strategy_orders_create_time;
DROP INDEX IF EXISTS idx_strategy_orders_login_id;

-- 删除表
DROP TABLE IF EXISTS strategy_orders;
DROP TABLE IF EXISTS strategy_pending_orders;

-- @downgrade_end

-- ============================================================================
-- VALIDATION SECTION (Optional)
-- ============================================================================
-- @validation_begin

-- 验证表是否创建成功
SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='strategy_pending_orders';
-- Expected result: 1

SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='strategy_orders';
-- Expected result: 1

-- 验证索引是否创建成功
SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name LIKE 'idx_strategy_%';
-- Expected result: 6

-- @validation_end