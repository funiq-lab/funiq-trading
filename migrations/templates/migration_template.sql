-- Migration: {MIGRATION_NAME}
-- Created: {TIMESTAMP}
-- Description: {DESCRIPTION}

-- Revision identifiers, used by Migration Manager
-- revision: {VERSION}
-- down_revision: {DOWN_REVISION}

-- ============================================================================
-- UPGRADE SECTION
-- ============================================================================
-- @upgrade_begin

-- Add your upgrade SQL statements here
-- Example:
-- ALTER TABLE strategy_pending_orders ADD COLUMN login_id TEXT;
-- CREATE INDEX idx_strategy_pending_orders_login_id ON strategy_pending_orders(login_id);

-- @upgrade_end

-- ============================================================================
-- DOWNGRADE SECTION  
-- ============================================================================
-- @downgrade_begin

-- Add your downgrade SQL statements here
-- Example:
-- DROP INDEX IF EXISTS idx_strategy_pending_orders_login_id;
-- ALTER TABLE strategy_pending_orders DROP COLUMN login_id;

-- @downgrade_end

-- ============================================================================
-- VALIDATION SECTION (Optional)
-- ============================================================================
-- @validation_begin

-- Add validation queries to verify migration success
-- These should return expected results after upgrade
-- Example:
-- SELECT COUNT(*) FROM pragma_table_info('strategy_pending_orders') WHERE name='login_id';
-- Expected result: 1

-- @validation_end