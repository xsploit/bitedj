#include "util/db/sqltransaction.h"

#include <QSqlDriver>
#include <QVariant>
#include <cstring>
#ifdef __SQLITE3__
#include <sqlite3.h>
#endif

#include "util/logger.h"
#include "util/assert.h"


namespace {

const mixxx::Logger kLogger("SqlTransaction");

inline
bool beginTransaction(QSqlDatabase database) {
    if (!database.isOpen()) {
        // Should only happen during tests
        kLogger.warning()
                << "Failed to begin SQL database transaction on"
                << database.connectionName();
        return false;
    }
    if (database.transaction()) {
        if (kLogger.debugEnabled()) {
            kLogger.debug()
                    << "Started new SQL database transaction on"
                    << database.connectionName();
        }
        return true;
    } else {
        kLogger.warning()
                << "Failed to begin SQL database transaction on"
                << database.connectionName();
        return false;
    }
}

} // anonymous namespace

SqlTransaction::SqlTransaction(
        const QSqlDatabase& database)
    : m_database(database), // implicitly shared (not copied)
      m_active(beginTransaction(m_database)) {
}

SqlTransaction::SqlTransaction(
        SqlTransaction&& other)
    : m_database(std::move(other.m_database)), // implicitly shared (not moved)
      m_active(other.m_active) {
    other.release();
}

SqlTransaction::~SqlTransaction() {
    if (m_active && m_database.isOpen()) {
        rollback();
    }
}

void SqlTransaction::release() {
    m_active = false;
}

SqlTransaction::operator bool() const {
    if (!m_active) {
        return false;
    }
    if (!m_database.isOpen()) {
        m_active = false;
        return false;
    }
#ifdef __SQLITE3__
    // SQLite may end the transaction itself (e.g. RAISE(ROLLBACK)). A stale
    // wrapper must not let a later SAVEPOINT/RELEASE commit outside our BEGIN.
    // This uses the same Qt/native SQLite linkage contract as DbConnection.
    QVariant handle = m_database.driver()->handle();
    if (handle.isValid() && std::strcmp(handle.typeName(), "sqlite3*") == 0) {
        auto* sqlite = *static_cast<sqlite3**>(handle.data());
        if (!sqlite || sqlite3_get_autocommit(sqlite)) {
            m_active = false;
        }
    }
#endif
    return m_active;
}

bool SqlTransaction::commit() {
    if (!static_cast<bool>(*this)) {
        return false;
    }
    DEBUG_ASSERT(m_active);
    if (!m_database.isOpen()) {
        kLogger.warning()
                << "Failed to commit transaction: No open SQL database connection";
        return false;
    }
    if (m_database.commit()) {
        if (kLogger.debugEnabled()) {
            kLogger.debug()
                    << "Committed SQL database transaction on"
                    << m_database.connectionName();
        }
        release(); // commit/rollback only once
        return true;
    } else {
        kLogger.warning()
                << "Failed to commit SQL database transaction on"
                 << m_database.connectionName();
        return false;
    }
}

bool SqlTransaction::rollback() {
    if (!static_cast<bool>(*this)) {
        return false;
    }
    DEBUG_ASSERT(m_active);
    if (!m_database.isOpen()) {
        kLogger.warning()
                << "Failed to rollback transaction: No open SQL database connection";
        return false;
    }
    if (m_database.rollback()) {
        if (kLogger.debugEnabled()) {
            kLogger.debug()
                    << "Rolled back SQL database transaction on"
                    << m_database.connectionName();
        }
        release(); // commit/rollback only once
        return true;
    } else {
        kLogger.warning()
                << "Failed to rollback SQL database transaction on"
                << m_database.connectionName();
        return false;
    }
}
