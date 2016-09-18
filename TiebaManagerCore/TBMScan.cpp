﻿/*
Copyright (C) 2015  xfgryujk
http://tieba.baidu.com/f?kw=%D2%BB%B8%F6%BC%AB%C6%E4%D2%FE%C3%D8%D6%BB%D3%D0xfgryujk%D6%AA%B5%C0%B5%C4%B5%D8%B7%BD

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include <TBMScan.h>
#include <TBMScanEvent.h>

#include <TBMCoreConfig.h>
#include <TiebaOperate.h>
#include <TBMOperate.h>

#include <StringHelper.h>
#include <NetworkHelper.h>
#include <MiscHelper.h>


static const TCHAR PAGE_COUNT_LEFT[] = _T(",\"total_page\":");
static const TCHAR PAGE_COUNT_RIGHT[] = _T("}");


CTBMScan::CTBMScan(CTBMCoreConfig* config, CUserCache* userCache, CTBMOperate* operate, ILog* log) :
	m_config(config),
	m_userCache(userCache),
	m_operate(operate),
	m_log(log)
{

}

CTBMScan::~CTBMScan()
{
	StopScan();
	if (m_scanThread != nullptr && m_scanThread->joinable())
		m_scanThread->join();
}

// 开始扫描
void CTBMScan::StartScan(const CString& sPage)
{
	StopScan();
	if (m_scanThread != nullptr && m_scanThread->joinable())
		m_scanThread->join();
	m_scanThread.reset(new thread(&CTBMScan::ScanThread, this, sPage));
}

// 结束扫描
void CTBMScan::StopScan()
{
	m_stopScanFlag = TRUE;
}

// 正在扫描
BOOL CTBMScan::IsScanning()
{
	return m_scanThread != nullptr && IsThreadRunning(*m_scanThread);
}

// 扫描主题图片
void CTBMScan::ScanThreadImage()
{
	CString msg;
	BOOL forceToConfirm;
	int pos, length;
	for (const ThreadInfo& thread : m_threads)
	{
		if (m_stopScanFlag)
			break;
		__int64 tid = _ttoi64(thread.tid);
		if (m_userCache->m_ignoredTID.find(tid) == m_userCache->m_ignoredTID.end())
		{
			CCheckThreadIllegalEvent event_(thread, msg, forceToConfirm, pos, length);
			if (m_eventBus.Post(CheckThreadImageIllegalEvent, event_) && event_.result)
			{
				m_operate->AddConfirm(Operation(forceToConfirm, pos, length, thread.title, 
					unique_ptr<ThreadInfo>(new ThreadInfo(thread))));
				m_log->Log(_T("<a href=\"http://tieba.baidu.com/p/") + thread.tid + _T("\">")
					+ HTMLEscape(thread.title) + _T("</a>") + msg);
				m_userCache->m_ignoredTID.insert(tid);
			}
		}
	}
}

// 总扫描线程
void CTBMScan::ScanThread(CString sPage)
{
	if (!m_eventBus.Post(ScanThreadStartEvent))
		goto ScanThreadEnd;

	m_stopScanFlag = FALSE;

	// 初始化
	if (!CoInitializeHelper())
		return;

	{
		// 初始化页数
		int iPage = _ttoi(sPage);
		CString ignoreThread; // 忽略前几个主题
		ignoreThread.Format(_T("%d"), (iPage - 1) * 50);


		CString msg;
		BOOL forceToConfirm;
		int pos, length;
		while (!m_stopScanFlag)
		{
			if (!m_eventBus.Post(ScanOnceStartEvent))
				goto ScanOnceEnd;

#pragma warning(suppress: 28159)
			DWORD startTime = GetTickCount();

			// 获取主题列表
			if (!GetThreads(m_operate->m_tiebaOperate->GetForumName(), ignoreThread, m_threads))
			{
				if (m_stopScanFlag)
					break;
				if (!m_config->m_briefLog)
					m_log->Log(_T("<font color=red>获取主题列表失败，重新开始本轮</font>"));
				continue;
			}

			// 扫描主题
			for (const ThreadInfo& thread : m_threads)
			{
				if (m_stopScanFlag)
					break;
				__int64 tid = _ttoi64(thread.tid);
				if (m_userCache->m_ignoredTID.find(tid) == m_userCache->m_ignoredTID.end())
				{
					CCheckThreadIllegalEvent event_(thread, msg, forceToConfirm, pos, length);
					if (m_eventBus.Post(CheckThreadIllegalEvent, event_) && event_.result)
					{
						m_operate->AddConfirm(Operation(forceToConfirm, pos, length, thread.title, 
							unique_ptr<ThreadInfo>(new ThreadInfo(thread))));
						m_log->Log(_T("<a href=\"http://tieba.baidu.com/p/") + thread.tid + _T("\">")
							+ HTMLEscape(thread.title) + _T("</a>") + msg);
						m_userCache->m_ignoredTID.insert(tid);
					}
				}
			}
			
			BOOL imageScanned = FALSE;

			// 扫描帖子
			if (!m_config->m_onlyScanTitle && m_eventBus.Post(PreScanAllThreadsEvent))
			{
				m_threadIndex = 0;

				// 创建线程扫描帖子
				int threadCount = m_config->m_threadCount; // m_config->m_threadCount会变
				vector<thread> threads;
				for (int i = 0; i < threadCount; i++)
					threads.push_back(thread(&CTBMScan::ScanPostThread, this, i));

				// 等待扫描帖子时扫描主题图片
				ScanThreadImage();
				imageScanned = TRUE;

				// 等待扫描帖子线程结束
				for (thread& i : threads)
					i.join();
			}

			// 如果没有扫描帖子，在这里扫描主题图片
			if (!imageScanned)
				ScanThreadImage();

			if (!m_config->m_briefLog)
			{
				CString content;
#pragma warning(suppress: 28159)
				content.Format(_T("<font color=green>本轮扫描结束，用时%.3f秒</font>"), (float)(GetTickCount() - startTime) / 1000.0f);
				m_log->Log(content);
			}

		ScanOnceEnd:
			m_eventBus.Post(ScanOnceEndEvent);

			// 延时
			int count = m_config->m_scanInterval * 10;
			for (int i = 0; i < count; i++)
			{
				if (m_stopScanFlag)
					break;
				Sleep(100);
			}
		}
	}

	m_stopScanFlag = FALSE;

	if (!m_config->m_briefLog)
		m_log->Log(_T("<font color=green>扫描结束</font>"));

	CoUninitialize();

ScanThreadEnd:
	m_eventBus.Post(ScanThreadEndEvent);

	TRACE(_T("总扫描线程结束\n"));
}

// 扫描帖子线程
void CTBMScan::ScanPostThread(int threadID)
{
	if (!m_eventBus.Post(ScanPostThreadStartEvent, CScanPostThreadEvent(threadID)))
		goto ScanPostThreadEnd;

	// 初始化
	if (!CoInitializeHelper())
		return;

	{
		CString pageCount, src;
		map<__int64, int>::iterator historyReplyIt;
		m_threadIndexLock.lock();
		while (!m_stopScanFlag && m_threadIndex < (int)m_threads.size())
		{
			ThreadInfo& thread = m_threads[m_threadIndex++];
			m_threadIndexLock.unlock();
			if (m_userCache->m_deletedTID.find(_ttoi64(thread.tid)) != m_userCache->m_deletedTID.end()) // 已删
				goto Next;

			__int64 tid = _ttoi64(thread.tid);
			int reply = _ttoi(thread.reply);
			historyReplyIt = m_userCache->m_reply->find(tid);
			BOOL hasHistoryReply = historyReplyIt != m_userCache->m_reply->end();
			if (hasHistoryReply
				&& reply == historyReplyIt->second // 回复数减少时也扫描，防止漏掉
				&& thread.lastAuthor == (*m_userCache->m_lastAuthor)[tid]) // 判断最后回复人，防止回复数-1然后有新回复+1
			{
				// 无新回复，跳过
				historyReplyIt->second = reply;
				goto Next;
			}

			{
				CPreScanThreadEvent event_(thread, threadID);
				if (!m_eventBus.Post(PreScanThreadEvent, event_))
					goto Next;
			}


			// 获取第一页
			src = HTTPGet(_T("http://tieba.baidu.com/p/" + thread.tid));
			if (src == NET_TIMEOUT_TEXT)
			{
				if (!m_config->m_briefLog)
					m_log->Log(_T("<a href=\"http://tieba.baidu.com/p/") + thread.tid + _T("\">") + thread.title
					+ _T("</a> <font color=red>获取贴子列表失败(超时)，暂时跳过</font>"));
				goto Next;
			}

			// 判断贴吧ID，避免百度乱插其他吧的帖子
			{
				CString tmp = GetStringBetween(src, _T("PageData.forum"), _T("}"));
				tmp.Replace(_T("\r\n"), _T(""));
				std::wcmatch res;
				if (std::regex_search((LPCTSTR)tmp, res, FORUM_ID_NAME_REG)
					&& res[3].str().c_str() != m_operate->m_tiebaOperate->GetForumID())
					goto Next;
			}

			// 获取帖子页数
			pageCount = GetStringBetween(src, PAGE_COUNT_LEFT, PAGE_COUNT_RIGHT);
			if (pageCount == _T(""))
			{
				WriteString(src, _T("thread.txt"));
				if (!m_config->m_briefLog)
					m_log->Log(_T("<a href=\"http://tieba.baidu.com/p/") + thread.tid + _T("\">") + thread.title
					+ _T("</a> <font color=red>获取贴子列表失败(可能已被删)，暂时跳过</font>"));
				goto Next;
			}

			// 扫描帖子页
			int iPageCount = _ttoi(pageCount);
			BOOL res = ScanPostPage(thread, 1, hasHistoryReply, 0, src, threadID);
			if (iPageCount > 1 && !m_stopScanFlag)
				res = ScanPostPage(thread, iPageCount, hasHistoryReply, 0, _T(""), threadID);


			// 记录历史回复
			if (res)
			{
				(*m_userCache->m_reply)[tid] = reply;
				(*m_userCache->m_lastAuthor)[tid] = thread.lastAuthor;
			}

		Next:
			m_threadIndexLock.lock();
		}
		m_threadIndexLock.unlock();
	}

	CoUninitialize();
	
ScanPostThreadEnd:
	m_eventBus.Post(ScanPostThreadEndEvent, CScanPostThreadEvent(threadID));
	TRACE(_T("扫描帖子线程结束\n"));
}

// 扫描帖子页
BOOL CTBMScan::ScanPostPage(const ThreadInfo& thread, int page, BOOL hasHistoryReply,
	int ScanedCount, const CString& src, int threadID)
{
	if (!m_eventBus.Post(ScanPostPageEvent, CScanPostPageEvent(thread, threadID, page)))
		return FALSE;

	CString sPage;
	sPage.Format(_T("%d"), page);

	// 获取帖子列表
	vector<PostInfo> posts;
	vector<LzlInfo> lzls;
	GetPostsResult res = GetPosts(thread.tid, src, sPage, posts);
	switch (res)
	{
	case GET_POSTS_TIMEOUT:
	case GET_POSTS_DELETED:
		m_log->Log(_T("<a href=\"http://tieba.baidu.com/p/") + thread.tid + _T("\">") + thread.title
			+ _T("</a> <font color=red>获取贴子列表失败(") + (res == GET_POSTS_TIMEOUT ? _T("超时") :
			_T("可能已被删")) + _T(")，暂时跳过</font>"));
		return FALSE;
	}
	GetLzls(m_operate->m_tiebaOperate->GetForumID(), thread.tid, sPage, posts, lzls);

	CString msg;
	BOOL forceToConfirm;
	int pos, length;
	// 扫描帖子
	for (const PostInfo& post : posts)
	{
		if (m_stopScanFlag)
			return FALSE;
		__int64 pid = _ttoi64(post.pid);
		if (m_userCache->m_ignoredPID.find(pid) == m_userCache->m_ignoredPID.end())
		{
			CCheckPostIllegalEvent event_(post, msg, forceToConfirm, pos, length);
			if (m_eventBus.Post(CheckPostIllegalEvent, event_) && event_.result)
			{
				m_operate->AddConfirm(Operation(forceToConfirm, pos, length, thread.title, 
					unique_ptr<PostInfo>(new PostInfo(post))));
				m_log->Log(_T("<a href=\"http://tieba.baidu.com/p/") + thread.tid + _T("\">") + HTMLEscape(thread.title) +
					_T("</a> ") + post.floor + _T("楼") + msg);
				m_userCache->m_ignoredPID.insert(pid);
			}
		}
	}

	// 扫描楼中楼
	for (const LzlInfo& lzl : lzls)
	{
		if (m_stopScanFlag)
			return FALSE;
		CCheckLzlIllegalEvent event_(lzl, msg, forceToConfirm, pos, length);
		if (m_eventBus.Post(CheckLzlIllegalEvent, event_) && event_.result)
		{
			__int64 cid = _ttoi64(lzl.cid);
			if (m_userCache->m_ignoredLZLID.find(cid) == m_userCache->m_ignoredLZLID.end())
			{
				m_operate->AddConfirm(Operation(forceToConfirm, pos, length, thread.title, 
					unique_ptr<LzlInfo>(new LzlInfo(lzl))));
				m_log->Log(_T("<a href=\"http://tieba.baidu.com/p/") + thread.tid + _T("\">") + HTMLEscape(thread.title) +
					_T("</a> ") + lzl.floor + _T("楼回复") + msg);
				m_userCache->m_ignoredLZLID.insert(cid);
			}
		}
	}

	// 扫描帖子图片
	for (const PostInfo& post : posts)
	{
		if (m_stopScanFlag)
			return FALSE;
		__int64 pid = _ttoi64(post.pid);
		if (m_userCache->m_ignoredPID.find(pid) == m_userCache->m_ignoredPID.end())
		{
			CCheckPostIllegalEvent event_(post, msg, forceToConfirm, pos, length);
			if (m_eventBus.Post(CheckPostImageIllegalEvent, event_) && event_.result)
			{
				m_operate->AddConfirm(Operation(forceToConfirm, pos, length, thread.title, 
					unique_ptr<PostInfo>(new PostInfo(post))));
				m_log->Log(_T("<a href=\"http://tieba.baidu.com/p/") + thread.tid + _T("\">") + HTMLEscape(thread.title) +
					_T("</a> ") + post.floor + _T("楼") + msg);
				m_userCache->m_ignoredPID.insert(pid);
			}
		}
	}

	// 扫描楼中楼图片
	for (const LzlInfo& lzl : lzls)
	{
		if (m_stopScanFlag)
			return FALSE;
		__int64 cid = _ttoi64(lzl.cid);
		if (m_userCache->m_ignoredLZLID.find(cid) == m_userCache->m_ignoredLZLID.end())
		{
			CCheckLzlIllegalEvent event_(lzl, msg, forceToConfirm, pos, length);
			if (m_eventBus.Post(CheckLzlImageIllegalEvent, event_) && event_.result)
			{
				m_operate->AddConfirm(Operation(forceToConfirm, pos, length, thread.title, 
					unique_ptr<LzlInfo>(new LzlInfo(lzl))));
				m_log->Log(_T("<a href=\"http://tieba.baidu.com/p/") + thread.tid + _T("\">") + HTMLEscape(thread.title) +
					_T("</a> ") + lzl.floor + _T("楼回复") + msg);
				m_userCache->m_ignoredLZLID.insert(cid);
			}
		}
	}

	// 递归扫描上一页
	if (!hasHistoryReply) // 如果有历史回复前面几页很可能被扫描过了，不递归
	{
		if (++ScanedCount < m_config->m_scanPageCount) // 没达到最大扫描页数
		{
			if (--page < 2) // 扫描完
				return TRUE;
			return ScanPostPage(thread, page, FALSE, ScanedCount, _T(""), threadID);
		}
	}
	return TRUE;
}
