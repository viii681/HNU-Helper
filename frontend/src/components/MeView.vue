<template>
  <!-- ==================== 二级页 1：统一账号与授权连接管理（收纳解绑等破坏性操作） ==================== -->
  <div v-if="store.subPage === 'accounts'" class="me-root">
    <header class="page-header sub-header">
      <button type="button" class="icon-btn" @click="store.closeSubPage()">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6" /></svg>
        我的
      </button>
      <h1>账号与授权管理</h1>
      <span style="width: 56px"></span>
    </header>

    <div class="page-scroll">
      <div class="group-label">全部凭据均采用 AES-256-GCM 独立随机 IV 加密保存</div>
      <div class="card">
        <div v-for="p in providerRows" :key="p.key" class="list-row">
          <div>
            <div class="row-title strong">{{ p.title }}</div>
            <div class="row-sub">
              <template v-if="p.brief">
                账号 {{ p.brief.account || '已登记' }} · 状态：{{ p.brief.state === 'ok' ? '正常' : '待重连' }}
              </template>
              <template v-else>{{ p.desc }}</template>
            </div>
          </div>
          <div class="row-end">
            <button
              v-if="p.brief && p.key !== 'jwxt'"
              type="button"
              class="btn"
              style="min-height: 32px; padding: 0 12px; color: var(--danger)"
              @click="disconnectSingleProvider(p.key, p.title)"
            >
              断开
            </button>
            <span v-else-if="!p.brief" class="status-dot">未连接</span>
            <span v-else class="status-dot ok">主账号</span>
          </div>
        </div>
      </div>

      <!-- 主教务账号解绑退出收纳在二级页底部，并带二次确认防误触 -->
      <div v-if="store.authenticated" style="margin-top: 24px">
        <button
          v-if="!confirmUnbind"
          type="button"
          class="btn block"
          style="color: var(--danger)"
          @click="confirmUnbind = true"
        >
          解除海南大学教务绑定并退出登录
        </button>
        <div v-else class="card padded danger-surface">
          <div class="row-title strong" style="color: var(--danger)">确认解除绑定？</div>
          <div class="row-sub" style="margin: 6px 0 12px">
            将撤销当前浏览器登录会话，并清空服务端缓存的课表、成绩与加密教务凭据。
          </div>
          <div class="btn-grid-2">
            <button type="button" class="btn" @click="confirmUnbind = false">取消</button>
            <button
              type="button"
              class="btn"
              style="background: var(--danger); color: #fff"
              @click="executeUnbindAll"
            >
              确认解绑退出
            </button>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- ==================== 二级页 2：问题反馈（客户端+服务端双层脱敏日志） ==================== -->
  <div v-else-if="store.subPage === 'feedback'" class="me-root">
    <header class="page-header sub-header">
      <button type="button" class="icon-btn" @click="store.closeSubPage()">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6" /></svg>
        我的
      </button>
      <h1>问题反馈</h1>
      <span style="width: 56px"></span>
    </header>

    <div class="page-scroll">
      <form class="card padded" @submit.prevent="submitFeedback">
        <label class="field">
          <span class="field-label">遇到的问题或改进建议</span>
          <textarea
            v-model.trim="fbForm.desc"
            rows="4"
            class="input"
            style="padding: 10px 12px; height: auto"
            placeholder="例如：观澜湖校区某栋教学楼名称变更、某门课周次解析偏差等…"
          ></textarea>
        </label>

        <div class="field">
          <label class="toggle-row">
            <span>附带本机脱敏诊断日志（不含学号/密码/Cookie）</span>
            <input v-model="fbForm.attachLog" type="checkbox" />
          </label>
        </div>

        <div v-if="fbForm.attachLog" class="log-preview-box">
          <div class="field-label">即将上传的脱敏日志预览：</div>
          <pre>{{ previewLogText || '（当前暂无异常诊断日志记录）' }}</pre>
        </div>

        <button type="submit" class="btn primary block" :disabled="fbSubmitting">
          {{ fbSubmitting ? '正在提交…' : '提交反馈' }}
        </button>
      </form>
    </div>
  </div>

  <!-- ==================== 主视图：身份卡片 / 强智教务绑定 / 个性化设置 ==================== -->
  <div v-else class="me-root">
    <header class="page-header">
      <h1>我的</h1>
      <button
        v-if="store.authenticated"
        type="button"
        class="icon-btn"
        :class="{ spin: store.syncing }"
        @click="store.syncJwxt()"
      >
        同步教务
      </button>
    </header>

    <div class="page-scroll">
      <!-- 未绑定 或 教务会话过期需输一次验证码重连 -->
      <div
        v-if="!store.authenticated || (store.credState === 'expired' && !dismissExpiredCard) || showRebindForm"
        class="card padded"
      >
        <div class="flex-between">
          <div>
            <div class="row-title strong" style="font-size: 17px">
              {{ store.credState === 'expired' ? '教务单次会话已到期 · 快速重连' : '绑定海南大学强智教务系统' }}
            </div>
            <div class="row-sub" style="margin-top: 4px">
              对接 <code>jxgl.hainanu.edu.cn/jsxsd</code> 统一身份认证（LoginToXkLdap），密码经 AES-256-GCM 加密保护。
            </div>
            <div class="session-keep-badge">
              <span class="sk-dot"></span>
              本地会话长效保留 {{ store.sessionTtlDays }} 天 · 后台每 8 分钟自动保活续期
            </div>
          </div>
          <button
            v-if="store.authenticated && (showRebindForm || store.credState === 'expired')"
            type="button"
            class="link-btn"
            @click="showRebindForm = false; dismissExpiredCard = true"
          >收起</button>
        </div>

        <!-- 实时登录/同步进度条 -->
        <SyncProgressBar
          v-if="(jwLoggingIn || store.syncing) && store.progressTask.active"
          variant="inline"
        />

        <!-- 已登录仅教务单次会话到期时：提供一键免密自动续期入口 -->
        <div v-if="store.authenticated && store.credState === 'expired' && !showRebindForm" class="quick-relink-bar">
          <span class="row-sub" style="margin: 0">
            本地已安全保留学号、加密密码与全部课表缓存，可先尝试免密续期：
          </span>
          <button
            type="button"
            class="btn"
            style="min-height: 34px; padding: 0 12px; font-size: 13px; flex-shrink: 0"
            :disabled="store.syncing || jwLoggingIn"
            @click="trySilentReconnect"
          >
            {{ store.syncing ? '续期中…' : '一键免密续期' }}
          </button>
        </div>

        <form style="margin-top: 14px" @submit.prevent="submitJwLogin">
          <label class="field">
            <span class="field-label">海南大学学号</span>
            <input
              v-model.trim="jwForm.username"
              type="text"
              class="input"
              autocomplete="username"
              placeholder="输入 10 位或 11 位海南大学学号（如 2024300128）"
            />
          </label>

          <label class="field">
            <div class="flex-between" style="margin-bottom: 6px">
              <span class="field-label" style="margin: 0">
                统一身份认证密码
                <small v-if="store.authenticated">（留空则自动使用已存加密密码）</small>
              </span>
              <a
                :href="forgotJwUrl"
                target="_blank"
                rel="noopener noreferrer"
                class="link-btn"
                style="min-height: 20px"
              >忘记密码？</a>
            </div>
            <div class="input-box">
              <input
                v-model="jwForm.password"
                :type="showJwPwd ? 'text' : 'password'"
                class="input"
                style="padding-right: 44px"
                autocomplete="current-password"
                :placeholder="store.authenticated ? '已存加密密码，留空即可直接重连同步' : '输入海南大学统一身份认证密码'"
              />
              <button
                type="button"
                class="pwd-eye"
                :aria-label="showJwPwd ? '隐藏密码' : '显示密码'"
                @click="showJwPwd = !showJwPwd"
              >
                <svg v-if="!showJwPwd" viewBox="0 0 24 24">
                  <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z" />
                  <circle cx="12" cy="12" r="3" />
                </svg>
                <svg v-else viewBox="0 0 24 24">
                  <path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24" />
                  <line x1="1" y1="1" x2="23" y2="23" />
                </svg>
              </button>
            </div>
          </label>

          <!-- 教务验证码（15 分钟本地保留、点击换图加载态提示；不填也可走海大统一身份认证免码直通） -->
          <div v-if="jwCaptcha.hasCaptcha" class="field">
            <div class="flex-between" style="margin-bottom: 6px">
              <span class="field-label" style="margin: 0">
                教务图形验证码（选填，点击图片刷新 · 本地保留 15 分钟）
              </span>
              <button type="button" class="link-btn" style="min-height: 20px" @click="loadJwCaptcha(true)">
                换一张
              </button>
            </div>
            <div class="captcha-row">
              <input
                v-model.trim="jwForm.captchaCode"
                type="text"
                class="input"
                placeholder="输入验证码（留空将自动走统一认证通道）"
              />
              <div class="captcha-wrap" @click="loadJwCaptcha(true)">
                <img
                  v-if="jwCaptcha.image"
                  :src="jwCaptcha.image"
                  alt="教务验证码"
                  class="captcha-img"
                  :style="{ opacity: captchaLoading ? 0.45 : 1 }"
                />
                <span v-if="captchaLoading" class="cap-spin-txt">刷新中…</span>
              </div>
            </div>
            <div v-if="jwCaptcha.liveAvailable === false && jwCaptcha.notice" class="warn-hint-banner">
              {{ jwCaptcha.notice }}
            </div>
          </div>

          <div v-if="jwError" class="err-banner">{{ jwError }}</div>

          <div class="btn-row" style="margin-top: 12px">
            <button type="submit" class="btn primary" style="flex: 1" :disabled="jwLoggingIn">
              {{ jwLoggingIn ? '正在登录并同步教务数据…' : '登录并同步海大教务' }}
            </button>
            <button
              v-if="store.meta?.demoEnabled"
              type="button"
              class="btn"
              @click="fillDemoStudent"
            >
              体验账号
            </button>
          </div>
        </form>
      </div>

      <!-- 已登录：海大学籍身份名片（学院 + 书院 + 校区） -->
      <button
        v-if="store.authenticated && store.profile"
        type="button"
        class="card padded profile-hero"
        @click="openProfileSheet"
      >
        <div class="avatar-circle">{{ (store.profile.name || '海')[0] }}</div>
        <div style="flex: 1; min-width: 0">
          <div class="prof-name-line">
            <span class="prof-name">{{ store.profile.name || '海大学子' }}</span>
            <span class="status-dot" :class="store.credState === 'ok' ? 'ok' : 'warn'">
              {{ store.credState === 'ok' ? '已同步 · 长期在线' : '本地缓存 · 点右上同步' }}
            </span>
          </div>
          <div class="row-sub" style="margin-top: 2px">
            学号 {{ store.profile.studentId }} · {{ store.profile.campus || '海甸校区' }}
          </div>
          <div class="prof-org-line">
            <span class="row-sub" style="margin: 0">{{ store.profile.college || store.profile.dept || '海南大学' }}</span>
            <span v-if="store.profile.academy" class="academy-pill">{{ store.profile.academy }}</span>
            <span v-else class="academy-pill missing" @click.stop="openProfileSheet">+ 设置所属书院</span>
          </div>
        </div>
        <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
      </button>

      <!-- 分组一：课表与作息个性化 -->
      <div class="group-label">课表、作息与课程调整</div>
      <div class="card">
        <button type="button" class="list-row tap" @click="openTermSheet">
          <div>
            <div class="row-title">学期与首周日期设置</div>
            <div class="row-sub">首周周一 {{ store.preferences.termStartDate }} · 共 {{ store.preferences.totalWeeks }} 周</div>
          </div>
          <div class="row-end">
            <span>第 {{ store.currentWeekOfToday }} 周</span>
            <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
          </div>
        </button>

        <button type="button" class="list-row tap" @click="openBellSheet">
          <div>
            <div class="row-title">11 节作息时间与校区预设</div>
            <div class="row-sub">
              {{ store.preferences.campusPreset === 'yazhou' ? '三亚崖州湾科教园预设' : '海甸/儋州/观澜湖标准预设 (07:40 早八)' }}
            </div>
          </div>
          <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
        </button>

        <button type="button" class="list-row tap" @click="showCourseAdjustSheet = true">
          <div>
            <div class="row-title">课程调整（自定义加课 / 隐藏恢复）</div>
            <div class="row-sub">
              自定义 {{ (store.preferences.customCourses || []).length }} 门 · 已隐藏 {{ (store.preferences.hiddenCourses || []).length + (store.preferences.hiddenSlots || []).length }} 处
            </div>
          </div>
          <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
        </button>

        <button
          type="button"
          class="list-row tap"
          @click="store.switchTab('schedule'); store.openSubPage('calendar')"
        >
          <div>
            <div class="row-title">系统日历订阅 (.ics 桌面小组件)</div>
            <div class="row-sub">将课表、考场与作业截止同步至手机锁屏与系统日历</div>
          </div>
          <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
        </button>
      </div>

      <!-- 分组二：账号、缓存与支持 -->
      <div class="group-label">账号安全、本地缓存与支持</div>
      <div class="card">
        <button type="button" class="list-row tap" @click="store.openSubPage('accounts')">
          <div>
            <div class="row-title">账号与连接管理</div>
            <div class="row-sub">管理教务主账号、学习通、MOOC 及预留平台连接</div>
          </div>
          <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
        </button>

        <button type="button" class="list-row tap" @click="openCacheSheet">
          <div>
            <div class="row-title">本地缓存清理与存储管理</div>
            <div class="row-sub">
              释放课表快照、公告与诊断日志缓存（保留登录状态）
            </div>
          </div>
          <div class="row-end">
            <span>{{ formatBytes(cacheStats.totalBytes) }}</span>
            <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
          </div>
        </button>

        <button type="button" class="list-row tap" @click="openThresholdSheet">
          <div>
            <div class="row-title">匿名统计与隐私设置</div>
            <div class="row-sub">{{ store.preferences.telemetryOptIn !== false ? '已开启每日匿名计数' : '已关闭匿名计数' }} · 日志全程脱敏</div>
          </div>
          <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
        </button>

        <button type="button" class="list-row tap" @click="openFeedbackSubPage">
          <div>
            <div class="row-title">问题反馈</div>
            <div class="row-sub">报告解析误差或建议（支持一键附上脱敏日志）</div>
          </div>
          <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
        </button>

        <button type="button" class="list-row tap" @click="showAboutSheet = true">
          <div>
            <div class="row-title">关于海大通</div>
            <div class="row-sub">v{{ store.meta?.about?.version || '1.0.0' }} · C++20 高并发后端与加密说明</div>
          </div>
          <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
        </button>
      </div>
    </div>

    <!-- ==================== 弹层 1：完整 14 项学籍卡片详情 + 十七书院选择 ==================== -->
    <div v-if="showProfileSheet && store.profile" class="sheet-backdrop" @click.self="showProfileSheet = false">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="showProfileSheet = false">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">海南大学学籍档案卡片</h3>
        <p class="sheet-sub">同步自强智教学一体化平台 · 支持补全或修改所属书院（共 17 个书院）</p>

        <!-- 所属书院与校区选择器（解决强智教务不返回书院字段的问题） -->
        <div class="card padded" style="background: var(--fill); margin-bottom: 12px">
          <div class="flex-between" style="margin-bottom: 8px">
            <span class="row-title strong">所属书院设置</span>
            <span class="row-sub" style="margin: 0">
              当前：<strong>{{ profileAcademyDraft || '未设置' }}</strong>（{{ profileCampusDraft || store.profile.campus || '海甸校区' }}）
            </span>
          </div>
          <div class="row-sub" style="margin-bottom: 10px">
            部分年级/专业在强智教务学籍卡中未包含书院字段，请直接点选您的所属书院（后续同步教务将自动保留）：
          </div>

          <div v-for="grp in store.campusAcademiesList" :key="grp.campusId" class="ac-campus-group">
            <div class="ac-campus-title">{{ grp.campusName }}（{{ grp.academies.length }}）</div>
            <div class="ac-chip-grid">
              <button
                v-for="ac in grp.academies"
                :key="ac"
                type="button"
                class="chip"
                :class="{ active: profileAcademyDraft === ac }"
                @click="pickAcademy(ac, grp.campusName)"
              >
                {{ ac }}
              </button>
            </div>
          </div>

          <button
            type="button"
            class="btn primary block"
            style="margin-top: 12px"
            :disabled="savingProfile"
            @click="saveProfileAcademy"
          >
            {{ savingProfile ? '正在上传保存…' : '保存所属书院与校区' }}
          </button>
        </div>

        <div class="card" style="background: var(--fill)">
          <div v-for="row in profileDetailRows" :key="row.label" class="list-row">
            <span class="row-sub">{{ row.label }}</span>
            <span class="row-title strong">{{ row.val || '—' }}</span>
          </div>
        </div>

        <button
          type="button"
          class="btn block"
          style="margin-top: 12px"
          @click="showProfileSheet = false; showRebindForm = true; dismissExpiredCard = false"
        >
          更新教务密码或切换学号
        </button>
      </div>
    </div>

    <!-- ==================== 弹层 1B：本地缓存清理与存储管理 ==================== -->
    <div v-if="showCacheSheet" class="sheet-backdrop" @click.self="showCacheSheet = false">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="showCacheSheet = false">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">本地缓存清理与存储管理</h3>
        <p class="sheet-sub">当前本地存储占用 {{ formatBytes(cacheStats.totalBytes) }} · 清理缓存不会退出登录</p>

        <SyncProgressBar
          v-if="cacheCleaning && store.progressTask.active"
          variant="inline"
        />

        <div class="cache-safe-banner">
          安全保障：您的登录令牌、AES-256-GCM 加密凭据、自定义加课与课程隐藏设置均会被安全保留。
        </div>

        <div class="card" style="background: var(--fill); margin-top: 10px">
          <label class="list-row tap cache-row">
            <div>
              <div class="row-title strong">课表、成绩与学业本地快照</div>
              <div class="row-sub">
                {{ formatBytes(cacheStats.items.bundle.bytes) }} · 含 {{ cacheStats.items.bundle.courses }} 门课表缓存
              </div>
            </div>
            <input v-model="cachePick.bundle" type="checkbox" />
          </label>

          <label class="list-row tap cache-row">
            <div>
              <div class="row-title strong">海南大学通知公告本地缓存</div>
              <div class="row-sub">
                {{ formatBytes(cacheStats.items.notices.bytes) }} · 共 {{ cacheStats.items.notices.count }} 条公告
              </div>
            </div>
            <input v-model="cachePick.notices" type="checkbox" />
          </label>

          <label class="list-row tap cache-row">
            <div>
              <div class="row-title strong">校历、海大指南与十七书院元数据</div>
              <div class="row-sub">{{ formatBytes(cacheStats.items.meta.bytes) }} · 清理后自动拉取最新字典</div>
            </div>
            <input v-model="cachePick.meta" type="checkbox" />
          </label>

          <label class="list-row tap cache-row">
            <div>
              <div class="row-title strong">本机脱敏诊断日志与临时缓存</div>
              <div class="row-sub">
                {{ formatBytes(cacheStats.items.logs.bytes + cacheStats.items.other.bytes) }} · 共 {{ cacheStats.items.logs.count }} 条日志
              </div>
            </div>
            <input v-model="cachePick.logs" type="checkbox" />
          </label>

          <label class="list-row tap cache-row">
            <div>
              <div class="row-title strong">浏览器静态资源缓存 (Cache Storage)</div>
              <div class="row-sub">释放 PWA 离线资源占用，解决页面旧资源残留</div>
            </div>
            <input v-model="cachePick.browserCache" type="checkbox" />
          </label>

          <label v-if="store.authenticated" class="list-row tap cache-row">
            <div>
              <div class="row-title strong">同时重置云端业务快照（保留绑定）</div>
              <div class="row-sub">用于数据错乱时强制重建服务端课表/成绩/学习通快照</div>
            </div>
            <input v-model="cachePick.serverSnapshots" type="checkbox" />
          </label>
        </div>

        <div class="btn-grid-2" style="margin-top: 14px">
          <button
            type="button"
            class="btn"
            :disabled="cacheCleaning || !hasAnyCachePicked"
            @click="executeCacheClean(false)"
          >
            {{ cacheCleaning ? '清理中…' : '仅清理所选缓存' }}
          </button>
          <button
            type="button"
            class="btn primary"
            :disabled="cacheCleaning || !hasAnyCachePicked"
            @click="executeCacheClean(true)"
          >
            {{ cacheCleaning ? '处理中…' : '清理并重新同步' }}
          </button>
        </div>
      </div>
    </div>

    <!-- ==================== 弹层 2：学期首周设置 ==================== -->
    <div v-if="showTermSheet" class="sheet-backdrop" @click.self="showTermSheet = false">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="showTermSheet = false">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">学期与首周周一设置</h3>
        <p class="sheet-sub">用于精确推算当前教学周次与课表日历日期</p>

        <label class="field">
          <span class="field-label">本学期第 1 周星期一日期</span>
          <input v-model="termForm.termStartDate" type="date" class="input" />
        </label>
        <label class="field">
          <span class="field-label">本学期总教学周数</span>
          <input v-model.number="termForm.totalWeeks" type="number" min="12" max="26" class="input" />
        </label>

        <button type="button" class="btn primary block" @click="saveTermSettings">保存设置</button>
      </div>
    </div>

    <!-- ==================== 弹层 3：海南大学 11 节作息预设与微调 ==================== -->
    <div v-if="showBellSheet" class="sheet-backdrop" @click.self="showBellSheet = false">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="showBellSheet = false">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">海南大学 11 节作息时间</h3>
        <p class="sheet-sub">切换校区预设或逐节微调上下课铃声</p>

        <div class="btn-grid-2" style="margin-bottom: 12px">
          <button
            type="button"
            class="btn"
            :class="{ primary: bellPresetDraft === 'haidian' }"
            @click="applyBellPreset('haidian')"
          >
            海甸/儋州/观澜湖 (07:40起)
          </button>
          <button
            type="button"
            class="btn"
            :class="{ primary: bellPresetDraft === 'yazhou' }"
            @click="applyBellPreset('yazhou')"
          >
            三亚崖州湾科教园 (08:00起)
          </button>
        </div>

        <div class="bell-edit-list">
          <div v-for="(pair, idx) in bellPeriodsDraft" :key="idx" class="bell-row">
            <span class="bell-lbl">第 {{ idx + 1 }} 节</span>
            <input v-model="pair[0]" type="time" class="input compact-time" />
            <span>–</span>
            <input v-model="pair[1]" type="time" class="input compact-time" />
          </div>
        </div>

        <button type="button" class="btn primary block" style="margin-top: 12px" @click="saveBellSettings">
          保存作息时间
        </button>
      </div>
    </div>

    <!-- ==================== 弹层 4：自定义加课（网格点选）与隐藏恢复 ==================== -->
    <div v-if="showCourseAdjustSheet" class="sheet-backdrop" @click.self="showCourseAdjustSheet = false">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="showCourseAdjustSheet = false">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">课程调整与自定义加课</h3>
        <p class="sheet-sub">支持添加旁听课/社团值班/考研自习，或一键恢复已隐藏的教务课</p>

        <!-- 恢复已隐藏课程 -->
        <div
          v-if="(store.preferences.hiddenCourses || []).length > 0 || (store.preferences.hiddenSlots || []).length > 0"
          class="card padded"
          style="background: var(--fill)"
        >
          <div class="flex-between">
            <div>
              <div class="row-title strong">已隐藏的课程与时段</div>
              <div class="row-sub">
                整门隐藏 {{ (store.preferences.hiddenCourses || []).length }} 门 · 单时段隐藏 {{ (store.preferences.hiddenSlots || []).length }} 处
              </div>
            </div>
            <button type="button" class="btn" @click="restoreAllHidden">全部恢复</button>
          </div>
        </div>

        <form @submit.prevent="addCustomCourse">
          <div class="btn-grid-2">
            <label class="field">
              <span class="field-label">课程 / 事项名称 *</span>
              <input v-model.trim="customForm.name" type="text" class="input" placeholder="如：高数旁听 / 实验室组会" />
            </label>
            <label class="field">
              <span class="field-label">地点 / 教室</span>
              <input v-model.trim="customForm.room" type="text" class="input" placeholder="如：5教201 / 信息楼" />
            </label>
          </div>

          <div class="btn-grid-2">
            <label class="field">
              <span class="field-label">开始周</span>
              <input v-model.number="customForm.weekStart" type="number" min="1" max="25" required class="input" />
            </label>
            <label class="field">
              <span class="field-label">结束周</span>
              <input v-model.number="customForm.weekEnd" type="number" min="1" max="25" required class="input" />
            </label>
          </div>

          <div class="field">
            <span class="field-label">点选上课星期与节次（可多选，共已选 {{ customPickedSlots.length }} 格）</span>
            <div class="mini-slot-picker">
              <div class="msp-head">
                <span></span>
                <span v-for="d in ['一', '二', '三', '四', '五', '六', '日']" :key="d">{{ d }}</span>
              </div>
              <div v-for="p in 11" :key="'mp' + p" class="msp-row">
                <span class="msp-p">{{ p }}</span>
                <button
                  v-for="dIdx in 7"
                  :key="`ms-${dIdx}-${p}`"
                  type="button"
                  class="msp-cell"
                  :class="{ picked: isCustomSlotPicked(dIdx - 1, p - 1) }"
                  @click="toggleCustomSlot(dIdx - 1, p - 1)"
                ></button>
              </div>
            </div>
          </div>

          <button type="submit" class="btn primary block">添加到我的课表</button>
        </form>
      </div>
    </div>

    <!-- ==================== 弹层 5：隐私偏好设置 ==================== -->
    <div v-if="showThresholdSheet" class="sheet-backdrop" @click.self="showThresholdSheet = false">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="showThresholdSheet = false">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">隐私与诊断偏好</h3>
        <p class="sheet-sub">所有本地与服务端日志均自动擦除学号、密码、Cookie 与完整 IP</p>

        <div class="field">
          <label class="toggle-row">
            <span>允许发送每日匿名活跃计数（不含任何学号/身份标识）</span>
            <input v-model="thresholdForm.telemetryOptIn" type="checkbox" />
          </label>
        </div>

        <button type="button" class="btn primary block" @click="saveThresholds">保存偏好</button>
      </div>
    </div>

    <!-- ==================== 弹层 6：关于海大通（底部深藏清除缓存按钮） ==================== -->
    <div v-if="showAboutSheet" class="sheet-backdrop" @click.self="showAboutSheet = false">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="showAboutSheet = false">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">{{ store.meta?.about?.name || '海大通' }} · {{ store.meta?.about?.fullName || '海南大学校园一体化服务系统' }}</h3>
        <p class="sheet-sub">版本 v{{ store.meta?.about?.version || '1.0.0' }} · C++20 全栈架构驱动</p>

        <div class="detail-box">
          <p class="summary-p">{{ store.meta?.about?.notice }}</p>
        </div>

        <div class="card" style="background: var(--fill); margin-top: 12px">
          <a
            v-for="cr in store.meta?.about?.credits || []"
            :key="cr.name"
            :href="cr.url"
            target="_blank"
            rel="noopener noreferrer"
            class="list-row tap"
            style="text-decoration: none"
          >
            <span class="row-title">{{ cr.name }}</span>
            <span class="row-end">↗</span>
          </a>
        </div>

        <!-- 低频破坏性操作「清除缓存」严格遵照 DESIGN.md 收纳在关于弹层最底部 -->
        <div style="margin-top: 18px; text-align: center">
          <button
            type="button"
            class="link-btn"
            style="color: var(--sub)"
            @click="clearAllSnapshotsCache"
          >
            遇到数据显示异常？清除快照缓存并重新同步
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { apiRequest, getAppLogText, setSessionToken } from '../api/client';
import { DEFAULT_HNU_BELL, inferCampusByAcademy, useAppStore } from '../stores/app';
import type { CourseSlot } from '../types';
import SyncProgressBar from './SyncProgressBar.vue';

const store = useAppStore();

// ==================== 强智教务登录与验证码 ====================
const jwForm = ref({ username: '', password: '', captchaCode: '' });
const showJwPwd = ref(false);
const jwLoggingIn = ref(false);
const jwError = ref('');
const showRebindForm = ref(false);
const dismissExpiredCard = ref(false);
const captchaLoading = ref(false);
const jwCaptcha = ref<{
  hasCaptcha: boolean;
  captchaId: string;
  image: string;
  liveAvailable?: boolean;
  notice?: string;
}>({
  hasCaptcha: false,
  captchaId: '',
  image: '',
  liveAvailable: true,
  notice: '',
});

const forgotJwUrl = computed(
  () => store.meta?.forgotPwdUrls?.jwxt || 'https://authserver.hainanu.edu.cn/retrieve-password/retrievePassword/index.html'
);

function applyCaptchaObj(cap: any) {
  if (!cap) return;
  const imgData = cap.img || cap.image || '';
  if (imgData || cap.captchaId) {
    jwCaptcha.value = {
      hasCaptcha: Boolean(imgData || cap.hasCaptcha),
      captchaId: cap.captchaId || '',
      image: imgData,
      liveAvailable: cap.liveAvailable !== false && cap.mode !== 'offline',
      notice: cap.notice || '',
    };
  }
}

async function loadJwCaptcha(reuseSession = false) {
  if (captchaLoading.value) return;
  captchaLoading.value = true;
  try {
    const qs = reuseSession && jwCaptcha.value.captchaId
      ? `?cid=${encodeURIComponent(jwCaptcha.value.captchaId)}`
      : '';
    const res = await apiRequest(`/api/v1/jw/captcha${qs}`);
    if (res?.ok) {
      applyCaptchaObj(res);
      jwForm.value.captchaCode = '';
    }
  } finally {
    captchaLoading.value = false;
  }
}

function fillDemoStudent() {
  jwForm.value.username = '2024300128';
  jwForm.value.password = 'hnu123456';
  jwForm.value.captchaCode = '';
}

async function trySilentReconnect() {
  jwError.value = '';
  const res = await store.syncJwxt();
  if (res?.ok) {
    dismissExpiredCard.value = true;
    showRebindForm.value = false;
  } else if (res?.error) {
    jwError.value = res.error;
    if (res.needCaptcha) dismissExpiredCard.value = false;
    if (res.captcha?.ok) applyCaptchaObj(res.captcha);
  }
}

async function submitJwLogin() {
  jwLoggingIn.value = true;
  jwError.value = '';
  const syncId = store.createSyncId('jw_login');
  store.startProgressTask({
    title: '正在登录并同步海大教务',
    stage: '连接海南大学统一身份认证…',
    detail: '认证后将同步课表、成绩、考场与学分',
    totalSteps: 6,
    kind: 'sync',
    pollSyncId: syncId,
    initialPercent: 6,
    autoCreepTo: 92,
  });
  try {
    const res = await apiRequest('/api/v1/jw/login', {
      username: jwForm.value.username || store.profile?.studentId || '',
      password: jwForm.value.password,
      captchaId: jwCaptcha.value.captchaId,
      captchaCode: jwForm.value.captchaCode,
      syncId,
    });
    if (res?.ok) {
      if (res.sessionToken) {
        setSessionToken(res.sessionToken);
      }
      if (typeof res.sessionTtlDays === 'number' && res.sessionTtlDays > 0) {
        store.sessionTtlDays = res.sessionTtlDays;
      }
      store.applyBundle(res);
      jwForm.value.password = '';
      jwForm.value.captchaCode = '';
      showRebindForm.value = false;
      dismissExpiredCard.value = false;
      store.finishProgressTask(true, `欢迎回来，${res.profile?.name || '海大学子'}！`, res.timetableError || '全部教务数据已同步并长效加密保存');
      store.showToast(
        res.timetableError
          ? `登录成功（${res.timetableError}）`
          : `欢迎你，${res.profile?.name || '海大学子'}！已同步海南大学课表与成绩`
      );
    } else {
      jwError.value = res?.error || '登录未通过，请核对学号密码';
      jwForm.value.captchaCode = '';
      store.finishProgressTask(
        false,
        res?.localServerDown ? '本地服务未启动' : (res?.kind === 'net' ? '连接教务超时' : '登录验证未通过'),
        jwError.value
      );
      if (res?.captcha?.ok) {
        applyCaptchaObj(res.captcha);
      } else if (res?.kind !== 'net') {
        await loadJwCaptcha(false);
      }
    }
  } finally {
    jwLoggingIn.value = false;
  }
}

watch(
  () => [store.authenticated, store.credState, showRebindForm.value],
  ([auth, cstate, rebind]) => {
    if (store.profile?.studentId && !jwForm.value.username) {
      jwForm.value.username = store.profile.studentId;
    }
    if (!auth || cstate === 'expired' || rebind) {
      if (cstate === 'expired') dismissExpiredCard.value = false;
      if (!jwCaptcha.value.image) {
        loadJwCaptcha();
      }
    }
  },
  { immediate: true }
);

watch(
  () => store.latestCaptcha,
  (cap) => {
    if (cap?.ok) {
      applyCaptchaObj(cap);
      if (store.credState === 'expired') dismissExpiredCard.value = false;
    }
  }
);

// ==================== 学籍详情（含 17 书院选择）、缓存清理、学期、作息、加课弹层 ====================
const showProfileSheet = ref(false);
const showCacheSheet = ref(false);
const showTermSheet = ref(false);
const showBellSheet = ref(false);
const showCourseAdjustSheet = ref(false);
const showThresholdSheet = ref(false);
const showAboutSheet = ref(false);
const confirmUnbind = ref(false);

const profileAcademyDraft = ref('');
const profileCampusDraft = ref('');
const savingProfile = ref(false);

function openProfileSheet() {
  profileAcademyDraft.value = store.profile?.academy || '';
  profileCampusDraft.value = store.profile?.campus || '海甸校区';
  showProfileSheet.value = true;
}

function pickAcademy(ac: string, campusName: string) {
  profileAcademyDraft.value = ac;
  profileCampusDraft.value = campusName || inferCampusByAcademy(ac) || profileCampusDraft.value;
}

async function saveProfileAcademy() {
  savingProfile.value = true;
  try {
    const res = await store.updateUserProfile(profileAcademyDraft.value, profileCampusDraft.value);
    if (res?.ok) {
      store.showToast(
        profileAcademyDraft.value
          ? `已设置所属书院为「${profileAcademyDraft.value}」`
          : '已更新学籍档案信息'
      );
      showProfileSheet.value = false;
    } else {
      store.showToast(res?.error || '保存书院信息失败');
    }
  } finally {
    savingProfile.value = false;
  }
}

// 本地缓存清理状态
const cacheStats = ref(store.getLocalCacheSummary());
const cacheCleaning = ref(false);
const cachePick = ref({
  bundle: true,
  notices: true,
  meta: true,
  logs: true,
  browserCache: true,
  serverSnapshots: false,
});

const hasAnyCachePicked = computed(
  () =>
    cachePick.value.bundle ||
    cachePick.value.notices ||
    cachePick.value.meta ||
    cachePick.value.logs ||
    cachePick.value.browserCache ||
    cachePick.value.serverSnapshots
);

function formatBytes(bytes: number): string {
  if (!bytes || bytes <= 0) return '0 B';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function refreshCacheStats() {
  cacheStats.value = store.getLocalCacheSummary();
}

function openCacheSheet() {
  refreshCacheStats();
  showCacheSheet.value = true;
}

async function executeCacheClean(resyncAfter: boolean) {
  cacheCleaning.value = true;
  try {
    await store.clearLocalCaches({ ...cachePick.value, resyncAfter });
    refreshCacheStats();
    showCacheSheet.value = false;
  } finally {
    cacheCleaning.value = false;
  }
}

const profileDetailRows = computed(() => {
  const p = store.profile;
  if (!p) return [];
  return [
    { label: '姓名', val: p.name },
    { label: '学号', val: p.studentId },
    { label: '所在校区', val: p.campus },
    { label: '专业学院', val: p.college || p.dept },
    { label: '所属书院', val: p.academy },
    { label: '修读专业', val: p.major },
    { label: '专业方向', val: p.direction },
    { label: '行政班级', val: p.className },
    { label: '所在年级', val: p.grade },
    { label: '培养层次', val: p.level },
    { label: '标准学制', val: p.duration },
    { label: '学籍状态', val: p.status },
    { label: '入学日期', val: p.enrolledAt },
    { label: '预计毕业', val: p.graduateAt },
  ];
});

const termForm = ref({ termStartDate: '2026-08-31', totalWeeks: 20 });
function openTermSheet() {
  termForm.value = {
    termStartDate: store.preferences.termStartDate || '2026-08-31',
    totalWeeks: store.preferences.totalWeeks || 20,
  };
  showTermSheet.value = true;
}

const FALLBACK_YAZHOU_BELL: string[][] = [
  ['08:00', '08:45'],
  ['08:55', '09:40'],
  ['10:00', '10:45'],
  ['10:55', '11:40'],
  ['14:30', '15:15'],
  ['15:25', '16:10'],
  ['16:30', '17:15'],
  ['17:25', '18:10'],
  ['19:20', '20:05'],
  ['20:15', '21:00'],
  ['21:10', '21:55'],
];

async function saveTermSettings() {
  const startDate = String(termForm.value.termStartDate || '').trim();
  const totalWeeks = Number(termForm.value.totalWeeks);
  if (!/^\d{4}-\d{2}-\d{2}$/.test(startDate)) {
    store.showToast('请选择有效的开学首周星期一日期');
    return;
  }
  if (!Number.isInteger(totalWeeks) || totalWeeks < 12 || totalWeeks > 26) {
    store.showToast('本学期总教学周数需为 12 ~ 26 之间的整数');
    return;
  }
  const res = await store.runTrackedTask(
    { title: '正在上传更新学期设置', stage: '保存开学首周与总教学周数…', kind: 'upload', doneStage: '学期设置已保存' },
    () => store.updatePreferences({ termStartDate: startDate, totalWeeks })
  );
  if (res?.ok) {
    showTermSheet.value = false;
    store.showToast('已更新开学首周与总周数');
  } else {
    store.showToast(res?.error || '保存学期设置失败');
  }
}

const bellPresetDraft = ref('haidian');
const bellPeriodsDraft = ref<string[][]>([]);

function openBellSheet() {
  bellPresetDraft.value = store.preferences.campusPreset || 'haidian';
  bellPeriodsDraft.value = store.activeBellPeriods.map((r) => [r[0], r[1]]);
  showBellSheet.value = true;
}

function applyBellPreset(key: 'haidian' | 'yazhou') {
  bellPresetDraft.value = key;
  const fallback = key === 'yazhou' ? FALLBACK_YAZHOU_BELL : DEFAULT_HNU_BELL;
  const preset = store.meta?.bellPresets?.[key]?.periods || fallback;
  bellPeriodsDraft.value = preset.map((r: string[]) => [r[0], r[1]]);
}

async function saveBellSettings() {
  for (let i = 0; i < bellPeriodsDraft.value.length; i++) {
    const [s, e] = bellPeriodsDraft.value[i] || ['', ''];
    if (!/^\d{2}:\d{2}$/.test(s) || !/^\d{2}:\d{2}$/.test(e) || s >= e) {
      store.showToast(`第 ${i + 1} 节上课时间须早于下课时间且不能为空`);
      return;
    }
  }
  const res = await store.runTrackedTask(
    { title: '正在上传更新作息表', stage: '保存海南大学 11 节作息配置…', kind: 'upload', doneStage: '11 节作息表已更新' },
    () =>
      store.updatePreferences({
        campusPreset: bellPresetDraft.value,
        bellSchedule: bellPeriodsDraft.value,
      })
  );
  if (res?.ok) {
    showBellSheet.value = false;
    store.showToast('已更新海南大学 11 节作息表');
  } else {
    store.showToast(res?.error || '保存作息时间失败');
  }
}

const customForm = ref({ name: '', room: '', weekStart: 1, weekEnd: 18 });
const customPickedSlots = ref<CourseSlot[]>([]);

function isCustomSlotPicked(day: number, period: number): boolean {
  return customPickedSlots.value.some((s) => s.day === day && s.period === period);
}

function toggleCustomSlot(day: number, period: number) {
  const idx = customPickedSlots.value.findIndex((s) => s.day === day && s.period === period);
  if (idx >= 0) customPickedSlots.value.splice(idx, 1);
  else customPickedSlots.value.push({ day, period });
}

async function addCustomCourse() {
  if (!customForm.value.name) {
    store.showToast('请填写自定义课程名称');
    return;
  }
  if (customPickedSlots.value.length === 0) {
    store.showToast('请在下方网格点选至少一个上课时段');
    return;
  }
  const rawWs = customForm.value.weekStart;
  const rawWe = customForm.value.weekEnd;
  if (
    rawWs === ('' as any) ||
    rawWe === ('' as any) ||
    !Number.isInteger(Number(rawWs)) ||
    !Number.isInteger(Number(rawWe)) ||
    Number(rawWs) < 1 ||
    Number(rawWs) > 25 ||
    Number(rawWe) < 1 ||
    Number(rawWe) > 25
  ) {
    store.showToast('开始周和结束周须为 1 ~ 25 之间的整数');
    return;
  }
  const ws = Math.min(Number(rawWs), Number(rawWe));
  const we = Math.max(Number(rawWs), Number(rawWe));
  const weeks = Array.from({ length: we - ws + 1 }, (_, i) => ws + i);
  const nextList = [
    ...(store.preferences.customCourses || []),
    {
      name: customForm.value.name,
      code: `CUSTOM_${Date.now()}`,
      teacher: '自定义',
      room: customForm.value.room,
      weeks,
      slots: [...customPickedSlots.value],
      isCustom: true,
    },
  ];
  const res = await store.updatePreferences({ customCourses: nextList });
  if (res?.ok) {
    customForm.value = { name: '', room: '', weekStart: 1, weekEnd: 18 };
    customPickedSlots.value = [];
    showCourseAdjustSheet.value = false;
    store.showToast('已添加到课表');
  } else {
    store.showToast(res?.error || '添加课程失败');
  }
}

async function restoreAllHidden() {
  const res = await store.updatePreferences({ hiddenCourses: [], hiddenSlots: [] });
  if (res?.ok) {
    store.showToast('已恢复全部隐藏的课程与时段');
  } else {
    store.showToast(res?.error || '恢复隐藏课程失败');
  }
}

const thresholdForm = ref({
  telemetryOptIn: store.preferences.telemetryOptIn !== false,
});

function openThresholdSheet() {
  thresholdForm.value = {
    telemetryOptIn: store.preferences.telemetryOptIn !== false,
  };
  showThresholdSheet.value = true;
}

async function saveThresholds() {
  const res = await store.updatePreferences({
    telemetryOptIn: thresholdForm.value.telemetryOptIn,
  });
  if (res?.ok) {
    showThresholdSheet.value = false;
    store.showToast('已保存隐私与诊断偏好');
  } else {
    store.showToast(res?.error || '保存隐私偏好失败');
  }
}

// ==================== 账号管理、缓存清理与反馈 ====================
const providerRows = computed(() => {
  const b = store.credBriefs || {};
  return [
    { key: 'jwxt', title: '强智教务一体化平台', desc: '主账号（课表 / 成绩 / 考场 / 学分）', brief: b.jwxt },
    { key: 'chaoxing', title: '超星学习通', desc: '本学期作业 / 测验 / 在线考试 / 随堂快查', brief: b.chaoxing },
    { key: 'mooc', title: '中国大学 MOOC', desc: '国家精品课互评作业与测验', brief: b.mooc },
    { key: 'yuketang', title: '雨课堂（预留槽位）', desc: '长江雨课堂账号备忘与直达', brief: b.yuketang },
    { key: 'zhihuishu', title: '智慧树（预留槽位）', desc: '跨校共享课账号备忘与直达', brief: b.zhihuishu },
    { key: 'zhidao', title: '知到（预留槽位）', desc: '智慧树移动版同步入口', brief: b.zhidao },
  ];
});

async function disconnectSingleProvider(key: string, title: string) {
  let endpoint = '';
  if (key === 'portal') endpoint = '/api/v1/portal/logout';
  else if (key === 'chaoxing') endpoint = '/api/v1/cx/logout';
  else if (key === 'mooc') endpoint = '/api/v1/mooc/logout';
  else endpoint = `/api/v1/ext/${key}/logout`;

  const res = await apiRequest(endpoint, {});
  if (res?.ok) {
    const nextBriefs = { ...(store.credBriefs || {}) };
    delete nextBriefs[key];
    store.credBriefs = nextBriefs;
    if (key === 'portal') {
      store.portalCard = null;
      store.portalApps = null;
    } else if (key === 'chaoxing') {
      store.cxTodo = null;
    } else if (key === 'mooc') {
      store.moocTodo = null;
    }
    store.showToast(`已断开 ${title}`);
  } else {
    store.showToast(res?.error || `断开 ${title} 失败`);
  }
}

async function executeUnbindAll() {
  confirmUnbind.value = false;
  await store.unbindAllAndLogout();
  store.closeSubPage();
}

async function clearAllSnapshotsCache() {
  showAboutSheet.value = false;
  openCacheSheet();
}

const fbForm = ref({ desc: '', attachLog: true });
const fbSubmitting = ref(false);
const previewLogText = ref('');

function openFeedbackSubPage() {
  previewLogText.value = getAppLogText();
  store.openSubPage('feedback');
}

async function submitFeedback() {
  if (!fbForm.value.desc && !(fbForm.value.attachLog && previewLogText.value)) {
    store.showToast('请填写问题描述');
    return;
  }
  fbSubmitting.value = true;
  try {
    const res = await store.runTrackedTask(
      {
        title: '正在上传脱敏反馈报告',
        stage: '执行客户端与服务端双重 PII 脱敏并上传…',
        kind: 'upload',
        doneStage: '脱敏反馈已安全送达',
      },
      () =>
        apiRequest('/api/v1/feedback', {
          desc: fbForm.value.desc,
          log: fbForm.value.attachLog ? previewLogText.value : '',
        })
    );
    if (res?.ok) {
      fbForm.value.desc = '';
      store.closeSubPage();
      store.showToast('感谢反馈！脱敏诊断报告已提交');
    } else {
      store.showToast(res?.error || '提交失败');
    }
  } finally {
    fbSubmitting.value = false;
  }
}

onMounted(() => {
  refreshCacheStats();
  if (!store.authenticated || store.credState === 'expired') {
    loadJwCaptcha();
  }
});
</script>

<style scoped>
.me-root {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}

.profile-hero {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 14px;
  text-align: left;
}

.avatar-circle {
  width: 52px;
  height: 52px;
  border-radius: 50%;
  background: var(--accent);
  color: #fff;
  font-size: 22px;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.prof-name-line {
  display: flex;
  align-items: center;
  gap: 10px;
}

.prof-name {
  font-size: 20px;
  font-weight: 700;
}

.flex-between {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.btn-row {
  display: flex;
  gap: 8px;
}

.btn-grid-2 {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
}

.err-banner {
  padding: 10px 12px;
  border-radius: var(--r-list);
  background: var(--danger-bg);
  color: var(--danger);
  font-size: 13px;
}

.captcha-wrap {
  position: relative;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  flex-shrink: 0;
}

.cap-spin-txt {
  position: absolute;
  font-size: 11px;
  font-weight: 700;
  color: var(--accent);
  background: rgba(255, 255, 255, 0.82);
  padding: 2px 6px;
  border-radius: 6px;
}

.bell-edit-list {
  max-height: 44dvh;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.bell-row {
  display: grid;
  grid-template-columns: 68px 1fr auto 1fr;
  align-items: center;
  gap: 6px;
}

.bell-lbl {
  font-size: 13px;
  font-weight: 600;
}

.compact-time {
  min-height: 36px;
  height: 36px;
  padding: 0 8px;
}

.mini-slot-picker {
  background: var(--fill);
  padding: 8px;
  border-radius: var(--r-list);
}

.msp-head,
.msp-row {
  display: grid;
  grid-template-columns: 24px repeat(7, 1fr);
  gap: 4px;
  align-items: center;
  margin-bottom: 4px;
  text-align: center;
  font-size: 11px;
}

.msp-p {
  color: var(--sub);
  font-weight: 600;
}

.msp-cell {
  height: 20px;
  border-radius: 4px;
  background: var(--card);
}

.msp-cell.picked {
  background: var(--accent);
}

.toggle-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 14px;
  cursor: pointer;
}

.log-preview-box {
  background: var(--fill);
  padding: 10px 12px;
  border-radius: var(--r-list);
  margin-bottom: 12px;
}

.log-preview-box pre {
  margin: 4px 0 0;
  font-size: 11px;
  color: var(--sub);
  max-height: 140px;
  overflow-y: auto;
  white-space: pre-wrap;
  word-break: break-all;
}

.detail-box {
  background: var(--fill);
  padding: 14px;
  border-radius: var(--r-card);
}

.summary-p {
  margin: 0;
  font-size: 14px;
  line-height: 1.6;
}

.session-keep-badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  margin-top: 8px;
  padding: 4px 10px;
  border-radius: 999px;
  background: rgba(52, 199, 89, 0.14);
  color: var(--ok);
  font-size: 12px;
  font-weight: 600;
}

.sk-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--ok);
}

.quick-relink-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-top: 12px;
  padding: 10px 12px;
  border-radius: var(--r-list);
  background: var(--fill);
}

.warn-hint-banner {
  margin-top: 8px;
  padding: 8px 10px;
  border-radius: var(--r-list);
  background: var(--warn-bg);
  color: var(--warn);
  font-size: 12px;
  line-height: 1.45;
}

.prof-org-line {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px;
  margin-top: 4px;
}

.academy-pill {
  display: inline-flex;
  align-items: center;
  padding: 2px 8px;
  border-radius: 999px;
  background: var(--accent-soft);
  color: var(--accent);
  font-size: 11px;
  font-weight: 600;
}

.academy-pill.missing {
  background: var(--warn-bg);
  color: var(--warn);
}

.ac-campus-group + .ac-campus-group {
  margin-top: 10px;
}

.ac-campus-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--sub);
  margin-bottom: 6px;
}

.ac-chip-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.cache-safe-banner {
  padding: 10px 12px;
  border-radius: var(--r-list);
  background: rgba(52, 199, 89, 0.12);
  color: var(--ok);
  font-size: 12px;
  line-height: 1.45;
}

.cache-row {
  cursor: pointer;
}

.cache-row input[type='checkbox'] {
  width: 18px;
  height: 18px;
  accent-color: var(--accent);
  flex-shrink: 0;
}
</style>
