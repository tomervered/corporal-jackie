#pragma once
// Runs work on Qt's thread pool and hands the result back to the owner's
// thread (rule 2: nothing slow on the UI thread). If the owner is deleted
// before the result arrives, the result is dropped.

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThreadPool>

#include <memory>
#include <utility>

namespace cj {

class Mailbox
{
public:
    // The receiving object lives on the thread that creates the Mailbox.
    // Workers may hold it after the owner is gone, so it is released with
    // deleteLater, which is safe from any thread.
    Mailbox() : m_box(new QObject, [](QObject* o) { o->deleteLater(); }) {}

    // work() runs on a pool thread; then(result) runs on the owner's thread.
    template <class Work, class Then>
    void run(QObject* owner, Work work, Then then)
    {
        std::shared_ptr<QObject> box = m_box;
        QPointer<QObject> guard(owner);
        QThreadPool::globalInstance()->start(
            [box, guard, work = std::move(work), then = std::move(then)]() mutable {
                auto result = work();
                QMetaObject::invokeMethod(
                    box.get(),
                    [guard, then = std::move(then), result = std::move(result)]() mutable {
                        if (guard)
                            then(std::move(result));
                    },
                    Qt::QueuedConnection);
            });
    }

private:
    std::shared_ptr<QObject> m_box;
};

} // namespace cj
