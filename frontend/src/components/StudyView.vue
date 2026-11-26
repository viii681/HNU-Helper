<template>
  <div class="study-root">
    <!-- 顶栏：主标题 + 平台归属眉标 + 刷新 -->
    <header class="page-header">
      <div style="display: flex; align-items: baseline">
        <h1>学习</h1>
        <span class="header-eyebrow">{{ isSchoolSub ? '学校教务平台' : '外部课程平台' }}</span>
      </div>
      <button
        type="button"
        class="icon-btn"
        :class="{ spin: busyRefreshing }"
        @click="refreshCurrentSubTab"
      >
        <svg viewBox="0 0 24 24">
          <path d="M21 12a9 9 0 0 0-15.5-6.36L3 8" />
          <path d="M3 3v5h5" />
          <path d="M3 12a9 9 0 0 0 15.5 6.36L21 16" />
          <path d="M16 16h5v5" />
        </svg>
        刷新
      </button>
    </header>

    <!-- 按数据来源分组的横向导航条：外部平台（含预留雨课堂/智慧树/知到） | 学校平台 -->
    <div class="chip-bar">
      <button
        v-for="p in EXT_TABS"
        :key="p.key"
        type="button"
        class="chip"
        :class="{ active: subTab === p.key }"
        @click="switchSubTab(p.key)"
      >
        {{ p.label }}
      </button>
      <span class="chip-divider"></span>
      <button
        v-for="s in SCHOOL_TABS"
        :key="s.key"
        type="button"
        class="chip"
        :class="{ active: subTab === s.key }"
        @click="switchSubTab(s.key)"
      >
        {{ s.label }}
      </button>
    </div>

    <div class="page-scroll">
      <!-- 未登录教务主会话时的统一拦截提示 -->
      <div v-if="!store.authenticated" class="card padded empty-state">
        <h2>请先绑定海南大学教务账号</h2>
        <p>绑定学号后，即可开启学习通/MOOC 截止扫描、查成绩绩点、看考场座位与毕业学分核对。</p>
        <button type="button" class="btn primary" @click="store.switchTab('me')">前往绑定学号</button>
      </div>

      <!-- ==================== 1. 超星学习通 ==================== -->
      <template v-else-if="subTab === 'chaoxing'">
        <!-- 未连接或需重新登录卡片 -->
        <div v-if="!cxStatus.linked || showCxLoginBox" class="card padded">
          <div class="row-title strong">连接超星学习通（只读安全模式）</div>
          <div class="row-sub" style="margin-top: 4px; margin-bottom: 12px">
            仅读取海南大学本学期在修课程的作业、测验、考试截止时间与通知，绝不代交作业或修改任何课程记录。
          </div>

          <!-- 已存密码一键重连模式 -->
          <div v-if="cxStatus.canRelink && !cxManualMode" style="margin-bottom: 10px">
            <div class="saved-hint-box">
              已加密保存账号 <strong>{{ cxStatus.user }}</strong> 的学习通凭据
            </div>
            <div class="btn-grid-2" style="margin-top: 10px">
              <button type="button" class="btn primary" :disabled="cxLoggingIn" @click="submitCxLogin(true)">
                {{ cxLoggingIn ? '重连中…' : '一键免密重连' }}
              </button>
              <button type="button" class="btn" @click="cxManualMode = true">改填新密码</button>
            </div>
          </div>

          <form v-else @submit.prevent="submitCxLogin(false)">
            <label class="field">
              <span class="field-label">学习通手机号 / 账号</span>
              <input
                v-model.trim="cxForm.user"
                type="text"
                class="input"
                autocomplete="username"
                placeholder="输入超星学习通绑定手机号"
              />
            </label>
            <label class="field">
              <div class="flex-between" style="margin-bottom: 6px">
                <span class="field-label" style="margin: 0">登录密码</span>
                <a
                  :href="forgotCxUrl"
                  target="_blank"
                  rel="noopener noreferrer"
                  class="link-btn"
                  style="min-height: 20px"
                >忘记密码？</a>
              </div>
              <div class="input-box">
                <input
                  v-model="cxForm.pass"
                  :type="showCxPwd ? 'text' : 'password'"
                  class="input"
                  style="padding-right: 44px"
                  autocomplete="current-password"
                  placeholder="AES-256-GCM 加密存储"
                />
                <button
                  type="button"
                  class="pwd-eye"
                  :aria-label="showCxPwd ? '隐藏密码' : '显示密码'"
                  @click="showCxPwd = !showCxPwd"
                >
                  <svg v-if="!showCxPwd" viewBox="0 0 24 24">
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

            <div class="btn-row">
              <button type="submit" class="btn primary" style="flex: 1" :disabled="cxLoggingIn">
                {{ cxLoggingIn ? '正在验证并连接…' : '登录并扫描本学期作业' }}
              </button>
              <button
                v-if="store.meta?.demoEnabled"
                type="button"
                class="btn"
                @click="fillCxDemo"
              >演示账号</button>
            </div>
          </form>
        </div>

        <!-- 已连接学习通工作面板 -->
        <template v-else>
          <div class="card padded">
            <div class="flex-between">
              <div>
                <span class="status-dot ok">学习通已连接（{{ cxStatus.user || '已授权' }}）</span>
                <div class="row-sub" style="margin-top: 4px">
                  已按海大课表白名单扫描 {{ store.cxTodo?.coursesScanned ?? 0 }} 门课（自动跳过结课旧课 {{ store.cxTodo?.coursesRested ?? 0 }} 门）
                </div>
              </div>
              <button type="button" class="link-btn" @click="showCxLoginBox = true">重连</button>
            </div>

            <!-- 扫描实时进度条（对齐后端 ScanManager 的 state / i / n / course / err 字段，附流动光泽动画） -->
            <div v-if="cxScanState?.state === 'scanning'" class="scan-progress-box">
              <div class="flex-between" style="font-size: 12px">
                <span style="font-weight: 600">
                  正在扫描：{{ cxScanState.course || cxScanState.phase || '获取课程列表…' }}
                </span>
                <span style="color: var(--accent); font-weight: 700">
                  {{ cxScanState.n ? Math.round((cxScanState.i / cxScanState.n) * 100) + '%' : '连接中' }}
                  ({{ cxScanState.i || 0 }}/{{ cxScanState.n || '?' }})
                </span>
              </div>
              <div class="progress-track" style="margin-top: 6px">
                <div
                  class="progress-fill animated-fill"
                  :style="{ width: cxScanState.n ? Math.max(8, Math.round((cxScanState.i / cxScanState.n) * 100)) + '%' : '25%' }"
                ></div>
              </div>
            </div>
            <div v-else-if="cxScanState?.state === 'error' && cxScanState?.err" class="scan-err-box">
              {{ cxScanState.err }}
            </div>

            <div class="action-pills" style="margin-top: 12px">
              <button type="button" class="btn primary" :disabled="cxScanState?.state === 'scanning'" @click="triggerCxScan(true)">
                {{ cxScanState?.state === 'scanning' ? '后台扫描中…' : '扫描作业与测验' }}
              </button>
              <button type="button" class="btn" :disabled="cxLiveLoading" @click="runCxLiveCheck">
                {{ cxLiveLoading ? '快查中…' : '随堂练习快查' }}
              </button>
              <button type="button" class="btn" @click="openCxNoticeSheet">
                通知收件箱
              </button>
            </div>
          </div>

          <!-- 随堂快查即时结果 -->
          <div v-if="cxLiveResult" class="card padded" style="border-left: 4px solid var(--ok)">
            <div class="flex-between">
              <span class="row-title strong">随堂快查结果</span>
              <button type="button" class="link-btn" @click="cxLiveResult = null">收起</button>
            </div>
            <div class="row-sub" style="margin-top: 4px">
              {{ (cxLiveResult.items || []).length > 0 ? `发现 ${cxLiveResult.items.length} 项进行中的签到或随堂练习！` : '当前各课程暂无正在进行的签到或随堂练习。' }}
            </div>
            <div v-for="(act, idx) in cxLiveResult.items || []" :key="idx" class="live-act-item">
              <strong>{{ act.course }}</strong> · {{ act.title }}（{{ act.status || '进行中' }}）
            </div>
          </div>

          <!-- 待办 / 已完成筛选 -->
          <div class="sub-filter-row">
            <button
              v-for="f in ['todo', 'done', 'all'] as const"
              :key="f"
              type="button"
              class="chip"
              :class="{ active: workFilter === f }"
              @click="workFilter = f"
            >
              {{ f === 'todo' ? `待完成 (${cxFilteredCounts.todo})` : f === 'done' ? `已完成 (${cxFilteredCounts.done})` : '全部' }}
            </button>
          </div>

          <div v-if="cxDisplayItems.length === 0" class="card padded empty-state">
            <h2>{{ workFilter === 'todo' ? '太棒了，当前没有待交作业或测验' : '暂无记录' }}</h2>
            <p>课表日历头部的截止圆点已同步更新。</p>
          </div>
          <div v-else class="card">
            <div v-for="it in cxDisplayItems" :key="it.id || it.title" class="list-row">
              <div style="flex: 1; min-width: 0">
                <div class="row-title strong">
                  <span class="kind-tag" :class="it.kind">{{ it.kindName || '任务' }}</span>
                  {{ it.title }}
                </div>
                <div class="row-sub">
                  {{ it.course }} · {{ it.remainText || it.status }}
                </div>
              </div>
              <div class="row-end">
                <button
                  type="button"
                  class="done-toggle-btn"
                  :class="{ marked: isMarkedDone(it) }"
                  @click="toggleDoneMark(it)"
                >
                  {{ isMarkedDone(it) ? '已标完成' : '标为已交' }}
                </button>
                <a
                  v-if="it.url"
                  :href="it.url"
                  target="_blank"
                  rel="noopener noreferrer"
                  class="link-btn"
                >↗</a>
              </div>
            </div>
          </div>
        </template>
      </template>

      <!-- ==================== 2. 中国大学 MOOC ==================== -->
      <template v-else-if="subTab === 'mooc'">
        <div v-if="!moocStatus.linked || showMoocBindBox" class="card padded">
          <div class="row-title strong">绑定中国大学 MOOC（icourse163.org）</div>
          <div class="row-sub" style="margin-top: 4px; margin-bottom: 12px">
            由于浏览器跨站同源安全限制，请在电脑浏览器登录 <code>icourse163.org</code> 后按 F12 复制请求头中的 <code>Cookie</code>（含 <code>NTESSTUDYSI</code>）粘贴到下方，后端将使用 AES-256-GCM 加密托管并自动扫描互评作业与测验。
          </div>

          <label class="field">
            <span class="field-label">MOOC 登录 Cookie 凭据串</span>
            <textarea
              v-model.trim="moocCookieInput"
              rows="3"
              class="input"
              style="padding: 10px 12px; height: auto"
              placeholder="粘贴包含 NTESSTUDYSI=...; STUDY_INFO=... 的 Cookie 字符串"
            ></textarea>
          </label>

          <div class="btn-row">
            <button
              type="button"
              class="btn primary"
              style="flex: 1"
              :disabled="moocBinding"
              @click="submitMoocBind"
            >
              {{ moocBinding ? '验证并扫描课程中…' : '加密绑定并扫描 MOOC' }}
            </button>
            <button
              v-if="store.meta?.demoEnabled"
              type="button"
              class="btn"
              @click="bindMoocDemo"
            >演示数据</button>
          </div>
        </div>

        <template v-else>
          <div class="card padded">
            <div class="flex-between">
              <div>
                <span class="status-dot ok">MOOC 已绑定（{{ store.moocTodo?.user?.name || moocStatus.user || '学员' }}）</span>
                <div class="row-sub" style="margin-top: 4px">
                  在修国家精品课 {{ (store.moocTodo?.courses || []).length }} 门 · 待办 {{ moocDisplayItems.filter((x: any) => !isMarkedDone(x) && x.state === 'todo').length }} 项
                </div>
              </div>
              <div style="display: flex; gap: 10px">
                <button type="button" class="link-btn" :disabled="moocScanning" @click="triggerMoocScan">
                  {{ moocScanning ? '扫描中…' : '重新扫描' }}
                </button>
                <button type="button" class="link-btn" @click="showMoocBindBox = true">更新凭据</button>
              </div>
            </div>
          </div>

          <div v-if="moocDisplayItems.length === 0" class="card padded empty-state">
            <h2>暂无 MOOC 测验或互评作业</h2>
            <p>点击右上角「刷新」可重新同步最新章节发布的任务。</p>
          </div>
          <div v-else class="card">
            <div v-for="(it, idx) in moocDisplayItems" :key="idx" class="list-row">
              <div style="flex: 1; min-width: 0">
                <div class="row-title strong">
                  <span class="kind-tag" :class="it.kind">{{ it.kindName || '测验' }}</span>
                  {{ it.title }}
                </div>
                <div class="row-sub">
                  {{ it.course }} · {{ it.chapter || '' }}
                  <template v-if="it.endAt"> · {{ formatDeadline(it.endAt) }}</template>
                </div>
              </div>
              <div class="row-end">
                <button
                  v-if="it.state === 'todo'"
                  type="button"
                  class="done-toggle-btn"
                  :class="{ marked: isMarkedDone(it) }"
                  @click="toggleDoneMark(it)"
                >
                  {{ isMarkedDone(it) ? '已标完成' : '标为已交' }}
                </button>
                <span v-else-if="it.state === 'expired'" class="status-dot warn">已截止</span>
                <span v-else class="status-dot ok">{{ it.score != null ? `${it.score}分` : '已完成' }}</span>
              </div>
            </div>
          </div>
        </template>
      </template>

      <!-- ==================== 3. 预留扩展平台入口（雨课堂 / 智慧树 / 知到） ==================== -->
      <template v-else-if="subTab === 'yuketang' || subTab === 'zhihuishu' || subTab === 'zhidao'">
        <div class="card padded">
          <div class="flex-between">
            <div>
              <div class="row-title strong">{{ activeReservedMeta.name }} · 预留扩展入口</div>
              <div class="row-sub" style="margin-top: 4px">{{ activeReservedMeta.desc }}</div>
            </div>
            <span class="status-dot" :class="extState.linked ? 'ok' : ''">{{ extState.linked ? '已登记' : '待登记' }}</span>
          </div>

          <div style="margin-top: 14px">
            <a
              :href="extState.portalUrl || activeReservedMeta.homeUrl"
              target="_blank"
              rel="noopener noreferrer"
              class="btn primary block"
              style="text-decoration: none"
            >
              一键打开 {{ activeReservedMeta.name }} 官方网页端 ↗
            </a>
          </div>
        </div>

        <div class="group-label">统一账号保险箱备忘与插件槽位预绑定（AES-256-GCM 加密）</div>
        <form class="card padded" @submit.prevent="saveReservedPlatform">
          <label class="field">
            <span class="field-label">绑定手机号 / 海大学号</span>
            <input
              v-model.trim="extForm.account"
              type="text"
              class="input"
              :placeholder="extState.account || '输入该平台绑定的手机号或学号'"
            />
          </label>
          <label class="field">
            <span class="field-label">访问口令 / 密码备忘（选填，服务端 AES-256-GCM 加密保存）</span>
            <div class="input-box">
              <input
                v-model="extForm.secret"
                :type="showExtSecret ? 'text' : 'password'"
                class="input"
                style="padding-right: 44px"
                placeholder="不填则仅登记账号与选课备注"
              />
              <button
                type="button"
                class="pwd-eye"
                :aria-label="showExtSecret ? '隐藏口令' : '显示口令'"
                @click="showExtSecret = !showExtSecret"
              >
                <svg viewBox="0 0 24 24">
                  <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z" />
                  <circle cx="12" cy="12" r="3" />
                </svg>
              </button>
            </div>
          </label>
          <label class="field">
            <span class="field-label">本学期在修课程备忘（如跨校共享课名称、班级邀请码）</span>
            <input
              v-model.trim="extForm.memo"
              type="text"
              class="input"
              placeholder="例如：南海历史文化选修 · 周日晚23:00截止看课"
            />
          </label>
          <button type="submit" class="btn block" :disabled="extSaving">
            {{ extSaving ? '正在加密保存…' : '保存到统一凭据保险箱' }}
          </button>
        </form>
      </template>

      <!-- ==================== 4. 学校平台：考试安排 + 教务通道探测 ==================== -->
      <template v-else-if="subTab === 'exams'">
        <!-- 教务待办通道快查（选课 / 评教 / 重修报名） -->
        <div class="card padded">
          <div class="flex-between">
            <div>
              <div class="row-title strong">强智教务实时通道探测</div>
              <div class="row-sub">检测当前是否处于选课轮次、期末评教或缓考重修开放窗口</div>
            </div>
            <button type="button" class="btn" :disabled="jwTodoLoading" @click="probeJwTodo">
              {{ jwTodoLoading ? '探测中…' : '立即检测' }}
            </button>
          </div>
          <div v-if="jwTodoItems.length > 0" style="margin-top: 10px">
            <div v-for="ch in jwTodoItems" :key="ch.key" class="channel-row">
              <span class="status-dot" :class="ch.state === 'open' ? 'ok' : ''">{{ ch.name }}</span>
              <span class="row-sub">{{ ch.text }} {{ ch.detail ? `(${ch.detail})` : '' }}</span>
            </div>
          </div>
        </div>

        <!-- 学期切换芯片 -->
        <div v-if="store.semesters.length > 0" class="sub-filter-row">
          <button
            v-for="sem in store.semesters"
            :key="sem.id"
            type="button"
            class="chip"
            :class="{ active: activeExamSem === sem.id }"
            @click="loadExamsForSem(sem.id)"
          >
            {{ sem.id }}
          </button>
        </div>

        <!-- 严格区分「没读到（报错）」与「暂未公布排考」 -->
        <div v-if="examViewData?.error" class="card padded danger-surface">
          <div class="row-title strong" style="color: var(--danger)">未能读取考场安排</div>
          <div class="row-sub">{{ examViewData.error }}</div>
        </div>
        <div v-else-if="(examViewData?.exams || []).length === 0" class="card padded empty-state">
          <h2>该学期暂未公布考场安排</h2>
          <p>海南大学期末集中考试通常在第 16–17 周由本科生院统一排考发布。</p>
        </div>
        <div v-else class="card">
          <div v-for="(ex, idx) in examViewData?.exams || []" :key="ex.code + idx" class="list-row">
            <div style="flex: 1">
              <div class="row-title strong">{{ ex.name }}</div>
              <div class="row-sub">
                {{ ex.date }} {{ ex.arrange }} ({{ ex.weekText }})
              </div>
              <div class="row-sub">
                {{ ex.campus || '海甸校区' }} · <strong>{{ ex.room || '考场待定' }}</strong> · 座号 <strong>{{ ex.seat || '—' }}</strong> · {{ ex.category }}
              </div>
            </div>
            <div class="row-end">
              <span class="exam-cd-badge" :class="{ urgent: examDaysDiff(ex.date) >= 0 && examDaysDiff(ex.date) <= 7 }">
                {{ formatExamCountdown(ex.date) }}
              </span>
            </div>
          </div>
        </div>
      </template>

      <!-- ==================== 5. 学校平台：成绩单（期中 / 期末 / 平时 / 实验 / 总评）与绩点趋势 ==================== -->
      <template v-else-if="subTab === 'grades'">
        <!-- 学期切换：点击即向教务实时查询该学期成绩 -->
        <div class="sub-filter-row">
          <button
            type="button"
            class="chip"
            :class="{ active: activeGradeSem === '' }"
            :disabled="gradesQuerying"
            @click="queryGradesForSem('')"
          >
            全部学期
          </button>
          <button
            v-for="sem in gradeSemChips"
            :key="sem"
            type="button"
            class="chip"
            :class="{ active: activeGradeSem === sem }"
            :disabled="gradesQuerying"
            @click="queryGradesForSem(sem)"
          >
            {{ sem }}
          </button>
        </div>

        <div class="card padded grade-query-bar">
          <div style="flex: 1; min-width: 0">
            <div class="row-title strong">实时查询 {{ activeGradeSem || '全部学期' }} 成绩</div>
            <div class="row-sub">
              自动汇总教务系统的平时、期中、期末、实验与总评成绩；期中成绩老师录入后即可查到
            </div>
          </div>
          <button type="button" class="btn primary" :disabled="gradesQuerying" @click="queryGradesForSem(activeGradeSem)">
            {{ gradesQuerying ? '查询中…' : '立即查询' }}
          </button>
        </div>

        <div v-if="gradeViewData?.evalBlocked" class="card padded danger-surface">
          <div class="row-title strong" style="color: var(--danger)">需先完成教学评价</div>
          <div class="row-sub">
            {{ gradeViewData.notice || '教务系统提示需先完成本学期评教后才能查看成绩，请前往教务系统完成评教后再查询。' }}
          </div>
          <a href="https://jxgl.hainanu.edu.cn/jsxsd/" target="_blank" rel="noopener noreferrer" class="link-btn">
            前往教务系统评教 ↗
          </a>
        </div>
        <div v-if="gradeViewData?.error" class="card padded danger-surface">
          <div class="row-title strong" style="color: var(--danger)">成绩读取失败</div>
          <div class="row-sub">{{ gradeViewData.error }}</div>
        </div>
        <template v-else>
          <!-- 总评概览卡片（仅统计已出总评的课程） -->
          <div class="card padded stats-3-grid">
            <div>
              <div class="stat-label">平均学分绩点 (GPA)</div>
              <div class="stat-num accent">{{ gradeViewData?.overall?.gpa || computedGpaSummary.gpa }}</div>
            </div>
            <div>
              <div class="stat-label">已出总评学分</div>
              <div class="stat-num">{{ gradeViewData?.overall?.credits || computedGpaSummary.credits }}</div>
            </div>
            <div>
              <div class="stat-label">
                {{ gradeStageCounts.overall > 0 ? '已出总评门数' : (gradeStageCounts.midterm > 0 ? '期中已出门数' : '已出总评门数') }}
              </div>
              <div class="stat-num">
                {{ gradeViewData?.overall?.count || (gradeStageCounts.overall > 0 ? gradeStageCounts.overall : gradeStageCounts.midterm) }}
              </div>
            </div>
          </div>

          <!-- 历年学期 GPA 趋势柱状图 -->
          <div v-if="(gradeViewData?.summary || []).length > 0" class="card padded">
            <div class="row-title strong" style="margin-bottom: 10px">各学期平均绩点趋势</div>
            <div class="gpa-bars">
              <div v-for="s in gradeViewData?.summary || []" :key="(s.semester || s.year + s.term)" class="gpa-bar-col">
                <span class="gpa-val">{{ s.gpa }}</span>
                <div class="gpa-bar-track">
                  <div
                    class="gpa-bar-fill"
                    :style="{ height: Math.min(100, Math.max(15, ( parseFloat(s.gpa || '0') / 4.0 ) * 100)) + '%' }"
                  ></div>
                </div>
                <span class="gpa-term">{{ s.semester || `${s.year}-${s.term}` }}</span>
              </div>
            </div>
          </div>

          <!-- 成绩阶段筛选：期中 / 期末 / 总评 -->
          <div class="sub-filter-row">
            <button
              v-for="st in GRADE_STAGES"
              :key="st.key"
              type="button"
              class="chip"
              :class="{ active: gradeStageFilter === st.key }"
              @click="gradeStageFilter = st.key"
            >
              {{ st.label }} ({{ gradeStageCounts[st.key] }})
            </button>
          </div>

          <!-- 筛选与搜索 -->
          <div class="grade-toolbar">
            <select v-model="gradeTermFilter" class="input" style="max-width: 160px; min-height: 36px">
              <option value="">全部学期</option>
              <option v-for="t in gradeTermOptions" :key="t" :value="t">{{ t }}</option>
            </select>
            <input
              v-model.trim="gradeKeyword"
              type="search"
              class="input"
              style="min-height: 36px"
              placeholder="搜索课程名或类别…"
            />
          </div>

          <div v-if="(gradeViewData?.courses || []).length === 0" class="card padded empty-state">
            <h2>{{ activeGradeSem || '全部学期' }}暂无已发布成绩记录</h2>
            <p>
              {{ gradeViewData?.notice || '教务系统本次没有返回成绩：期中成绩需任课教师录入并开放查询后才会显示，期末总评通常在考试后 1–2 周内发布。' }}
            </p>
            <div v-if="(gradeViewData?.sourcesChecked || []).length > 0" class="row-sub" style="margin: 8px 0">
              已检查通道：{{ gradeViewData.sourcesChecked.map((x: any) => `${x.label}(${x.count})`).join('、') }}
            </div>
            <div class="btn-row" style="justify-content: center">
              <button type="button" class="btn primary" :disabled="gradesQuerying" @click="queryGradesForSem(activeGradeSem)">
                {{ gradesQuerying ? '查询中…' : '重新查询期中 / 期末成绩' }}
              </button>
              <a href="https://jxgl.hainanu.edu.cn/jsxsd/" target="_blank" rel="noopener noreferrer" class="btn" style="text-decoration: none">
                打开教务系统核对 ↗
              </a>
            </div>
          </div>
          <div v-else-if="filteredGrades.length === 0" class="card padded empty-state">
            <h2>没有符合筛选条件的课程</h2>
            <p>请切换上方「期中 / 期末 / 总评」筛选或清空搜索词。</p>
          </div>
          <div v-else class="card">
            <div v-for="(g, idx) in filteredGrades" :key="g.code + g.term + idx" class="list-row grade-card-row">
              <div style="width: 100%">
                <div class="flex-between" style="align-items: flex-start">
                  <div style="flex: 1; min-width: 0">
                    <div class="row-title strong grade-title-line">
                      <span>{{ g.name }}</span>
                      <span class="grade-pill" :class="(g.nature || '').includes('选') ? 'elec' : 'req'">
                        {{ g.nature || '必修' }}
                      </span>
                      <span v-if="g.score" class="grade-pill req">总评已出</span>
                      <span v-else-if="g.finalScore" class="grade-pill req">期末已出</span>
                      <span v-else-if="g.midtermScore" class="grade-pill warn">期中已出</span>
                      <span v-if="g.examMethod" class="grade-pill">{{ g.examMethod }}</span>
                      <span v-if="g.examNature && g.examNature !== '正常考试'" class="grade-pill warn">
                        {{ g.examNature }}
                      </span>
                      <span v-if="g.scoreFlag" class="grade-pill warn">{{ g.scoreFlag }}</span>
                      <span v-if="g.generalCategory" class="grade-pill">{{ g.generalCategory }}</span>
                    </div>
                    <div class="row-sub" style="margin-top: 3px">
                      {{ g.term }}{{ g.code ? ` · ${g.code}` : '' }} · {{ g.category || g.nature }} · {{ g.credit }} 学分{{ g.hours ? ` · ${g.hours} 学时` : '' }}{{ g.makeupTerm ? ` · 补重:${g.makeupTerm}` : '' }}
                    </div>
                  </div>
                  <div class="grade-right">
                    <div class="score-main">{{ g.score || (g.finalScore ? g.finalScore : g.midtermScore ? `期中 ${g.midtermScore}` : '待总评') }}</div>
                    <div class="row-sub">GPA {{ g.gpa || '—' }}</div>
                  </div>
                </div>

                <!-- 平时 / 期中 / 期末（及实验）成绩构成明细栏 -->
                <div class="subscore-grid">
                  <div class="subscore-cell">
                    <span class="ss-label">平时{{ g.regularRatio ? ` (${g.regularRatio})` : '' }}</span>
                    <strong class="ss-val">{{ g.regularScore || '—' }}</strong>
                  </div>
                  <div class="subscore-cell">
                    <span class="ss-label">期中{{ g.midtermRatio ? ` (${g.midtermRatio})` : '' }}</span>
                    <strong class="ss-val">{{ g.midtermScore || '—' }}</strong>
                  </div>
                  <div class="subscore-cell">
                    <span class="ss-label">期末{{ g.finalRatio ? ` (${g.finalRatio})` : '' }}</span>
                    <strong class="ss-val">{{ g.finalScore || '—' }}</strong>
                  </div>
                  <div v-if="g.labScore" class="subscore-cell">
                    <span class="ss-label">实验{{ g.labRatio ? ` (${g.labRatio})` : '' }}</span>
                    <strong class="ss-val">{{ g.labScore }}</strong>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </template>
      </template>

      <!-- ==================== 6. 学校平台：学分核对与待选修审核 ==================== -->
      <template v-else-if="subTab === 'credits'">
        <div v-if="!store.credits?.ready" class="card padded empty-state">
          <h2>暂无培养方案学分分析</h2>
          <p>{{ store.credits?.error || '请点击右上角刷新教务数据以拉取专业培养方案。' }}</p>
        </div>
        <template v-else>
          <!-- 主概览卡：严格区分「完整毕业要求」与「大一/阶段已下发执行计划」 -->
          <div class="card padded">
            <div class="flex-between" style="align-items: flex-start">
              <div>
                <div class="row-sub">
                  {{ store.credits.scopeTitle || (store.credits.isGraduationPlan ? '海南大学本科专业培养方案毕业要求' : '当前教务已下发执行计划（阶段核对）') }}
                </div>
                <div class="cal-big-week">
                  {{ store.credits.totalEarned }}
                  <span class="cal-sub-total">
                    已获 / {{ store.credits.totalRequired }} {{ store.credits.isGraduationPlan ? '毕业要求学分' : '当前计划学分' }}
                  </span>
                </div>
              </div>
              <span class="seat-badge">本学期在修 {{ store.credits.totalStudying }} 学分</span>
            </div>

            <div class="progress-track dual-track" style="margin-top: 12px">
              <div
                class="progress-fill studying-layer"
                :style="{ width: Math.min(100, store.credits.percentWithStudying || 0) + '%' }"
              ></div>
              <div
                class="progress-fill"
                :style="{ width: Math.min(100, store.credits.percent || 0) + '%' }"
              ></div>
            </div>

            <div class="row-sub" style="margin-top: 8px">
              <template v-if="store.credits.isGraduationPlan">
                扣除已获 {{ store.credits.totalEarned }} 学分与本学期在修 {{ store.credits.totalStudying }} 学分后，距毕业要求还差 <strong>{{ store.credits.totalRemain }}</strong> 学分
              </template>
              <template v-else>
                扣除已获 {{ store.credits.totalEarned }} 学分与本学期在修 {{ store.credits.totalStudying }} 学分后，当前已下发执行计划还剩 <strong>{{ store.credits.totalRemain }}</strong> 学分待修
              </template>
            </div>

            <div v-if="!store.credits.isGraduationPlan" class="audit-warn-box">
              <strong>学分审核说明：</strong>{{ store.credits.scopeNote }}
              <template v-if="(store.credits.filteredIntlCredits || 0) > 0">
                （已自动为您剔除执行计划中面向来华留学生的替代汉语课程 {{ store.credits.filteredIntlCredits }} 学分{{ (store.credits.dedupSubCredits || 0) > 0 ? `及同组重复子项 ${store.credits.dedupSubCredits} 学分` : '' }}）
              </template>
            </div>
          </div>

          <!-- 毕业总要求与尚未选修的选修课审核卡（针对大一未选选修课场景专门补齐！） -->
          <div v-if="!store.credits.isGraduationPlan && gradAudit" class="card padded">
            <div class="flex-between">
              <div>
                <div class="row-title strong">毕业总学分与待选修模块审核</div>
                <div class="row-sub">{{ gradAudit.matchedMajor }}</div>
              </div>
              <button type="button" class="link-btn" @click="showGradTargetInput = !showGradTargetInput">
                {{ showGradTargetInput ? '收起' : '校准毕业总学分' }}
              </button>
            </div>

            <div v-if="showGradTargetInput" class="grad-target-editor">
              <input
                v-model.number="customGradTargetInput"
                type="number"
                min="120"
                max="220"
                step="0.5"
                class="input"
                style="min-height: 36px"
                placeholder="输入本专业培养方案毕业应修总学分（如 155 / 160）"
              />
              <button type="button" class="btn primary" style="min-height: 36px" @click="saveCustomGradTarget">
                保存
              </button>
            </div>

            <div v-if="gradAudit.gradTotalRequired > 0" class="grad-ref-summary">
              <div class="flex-between" style="font-size: 13px">
                <span>培养方案毕业应修总学分</span>
                <strong>
                  已获 {{ store.credits.totalEarned }} (+在修 {{ store.credits.totalStudying }}) / {{ gradAudit.gradTotalRequired }} 学分
                </strong>
              </div>
              <div class="progress-track dual-track" style="margin-top: 6px">
                <div
                  class="progress-fill studying-layer"
                  :style="{ width: Math.min(100, gradAudit.gradPercentWithStudying || 0) + '%' }"
                ></div>
                <div
                  class="progress-fill"
                  :style="{ width: Math.min(100, Math.round(((store.credits.totalEarned || 0) / gradAudit.gradTotalRequired) * 100)) + '%' }"
                ></div>
              </div>
              <div class="row-sub" style="margin-top: 6px">
                计入未选选修课与高年级课程后，距毕业总要求还需修读 <strong>{{ gradAudit.gradRemainAfterStudying }}</strong> 学分
              </div>
            </div>

            <div class="elective-audit-list">
              <div class="elec-audit-item">
                <div class="flex-between">
                  <span class="row-title strong">通识类选修课（待选修模块）</span>
                  <span class="elec-stat">
                    已修 {{ gradAudit.generalElectiveEarned }} (+{{ gradAudit.generalElectiveStudying }}在修) / 至少 {{ gradAudit.generalElectiveRequired }} 学分
                  </span>
                </div>
                <div class="row-sub" style="margin-top: 2px">
                  {{ gradAudit.generalElectiveRule }} · 仍需选修 <strong>{{ gradAudit.generalElectiveRemain }}</strong> 学分
                </div>
              </div>

              <div class="elec-audit-item">
                <div class="flex-between">
                  <span class="row-title strong">专业类选修课（待选修模块）</span>
                  <span class="elec-stat">
                    <template v-if="gradAudit.majorElectiveRequired > 0">
                      已修 {{ gradAudit.majorElectiveEarned }} (+{{ gradAudit.majorElectiveStudying }}在修) / 至少 {{ gradAudit.majorElectiveRequired }} 学分
                    </template>
                    <template v-else>
                      已修 {{ gradAudit.majorElectiveEarned }} (+{{ gradAudit.majorElectiveStudying }}在修) 学分
                    </template>
                  </span>
                </div>
                <div class="row-sub" style="margin-top: 2px">
                  <template v-if="gradAudit.majorElectiveRequired > 0">
                    大一暂未开放专业选修，后续学期仍需选修至少 <strong>{{ gradAudit.majorElectiveRemain }}</strong> 学分
                  </template>
                  <template v-else>
                    大一阶段通常暂未选修，具体最低学分以学院专业培养方案为准（一般 14–22 学分，可点右上校准总学分）
                  </template>
                </div>
              </div>

              <div v-if="gradAudit.unscheduledCompulsoryEst > 0" class="elec-audit-item">
                <div class="flex-between">
                  <span class="row-title strong">高年级待下发专业必修与毕业设计</span>
                  <span class="elec-stat">约 {{ gradAudit.unscheduledCompulsoryEst }} 学分</span>
                </div>
                <div class="row-sub" style="margin-top: 2px">
                  随大二至大四各学期教学执行计划下发后自动并入上方计划列表
                </div>
              </div>

              <div class="elec-audit-item">
                <div class="flex-between">
                  <span class="row-title strong">第二课堂成绩单（单独核算）</span>
                  <span class="elec-stat">毕业要求至少 {{ gradAudit.secondClassRequired }} 学分</span>
                </div>
                <div class="row-sub" style="margin-top: 2px">
                  含思想成长、志愿公益、创新创业与文体实践，不计入课程总学分内
                </div>
              </div>
            </div>
          </div>

          <div class="group-label">
            {{ store.credits.isGraduationPlan ? '各培养模块达标进度' : '当前已下发执行计划模块进度' }}
          </div>
          <div class="card padded">
            <div v-for="m in store.credits.modules || []" :key="m.name" class="mod-row">
              <div class="flex-between" style="font-size: 13px">
                <span class="row-title strong">{{ m.name }}</span>
                <span>{{ m.earned }} (+{{ m.studying }}在修) / {{ m.required }} 学分 · 剩 {{ m.remain }}</span>
              </div>
              <div class="progress-track dual-track" style="margin-top: 6px">
                <div
                  class="progress-fill studying-layer"
                  :style="{ width: Math.min(100, m.percentWithStudying || 0) + '%' }"
                ></div>
                <div class="progress-fill" :style="{ width: Math.min(100, m.percent || 0) + '%' }"></div>
              </div>
            </div>
          </div>

          <div class="sub-filter-row">
            <button
              v-for="cf in ['all', 'pending', 'studying', 'passed'] as const"
              :key="cf"
              type="button"
              class="chip"
              :class="{ active: creditFilter === cf }"
              @click="creditFilter = cf"
            >
              {{ cf === 'all' ? '全部已列课' : cf === 'pending' ? '待修课程' : cf === 'studying' ? '本学期在修' : '已修通过' }}
            </button>
          </div>

          <div class="card">
            <div v-for="c in filteredPlanCourses" :key="c.code + c.name" class="list-row">
              <div>
                <div class="row-title strong">{{ c.name }}</div>
                <div class="row-sub">
                  {{ c.cat }} · {{ c.credit }} 学分 · {{ c.required ? '必修' : '选修' }}{{ c.termLabel ? ` · ${c.termLabel}` : (c.terms?.length ? ` · 第${c.terms.join(',')}学期` : '') }}
                </div>
              </div>
              <div class="row-end">
                <span
                  class="status-dot"
                  :class="c.state === 'passed' ? 'ok' : c.state === 'studying' ? 'warn' : ''"
                >
                  {{ c.state === 'passed' ? `已过 (${c.score})` : c.state === 'studying' ? '在修' : '待修' }}
                </span>
              </div>
            </div>
          </div>
        </template>
      </template>
    </div>

    <!-- 超星学习通通知收件箱弹层 -->
    <div v-if="showCxNotices" class="sheet-backdrop" @click.self="showCxNotices = false">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="showCxNotices = false">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">学习通课程通知收件箱</h3>
        <p class="sheet-sub">教师发布的调课、实验地点变更与作业提醒</p>

        <div v-if="cxNoticesLoading" class="empty-state"><p>正在读取收件箱…</p></div>
        <div v-else-if="cxNoticesList.length === 0" class="empty-state"><p>收件箱暂无课程通知</p></div>
        <div v-else class="card" style="background: var(--fill)">
          <div v-for="(msg, i) in cxNoticesList" :key="msg.id || i" class="list-row">
            <div>
              <div class="row-title strong">{{ msg.title }}</div>
              <div class="row-sub">{{ msg.course }} · {{ msg.sender }} · {{ msg.time }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { apiRequest } from '../api/client';
import { useAppStore } from '../stores/app';

const store = useAppStore();

type SubTabKey = 'chaoxing' | 'mooc' | 'yuketang' | 'zhihuishu' | 'zhidao' | 'exams' | 'grades' | 'credits';

const EXT_TABS: Array<{ key: SubTabKey; label: string }> = [
  { key: 'chaoxing', label: '学习通' },
  { key: 'mooc', label: 'MOOC' },
  { key: 'yuketang', label: '雨课堂' },
  { key: 'zhihuishu', label: '智慧树' },
  { key: 'zhidao', label: '知到' },
];

const SCHOOL_TABS: Array<{ key: SubTabKey; label: string }> = [
  { key: 'exams', label: '考试' },
  { key: 'grades', label: '成绩' },
  { key: 'credits', label: '学分' },
];

const subTab = ref<SubTabKey>('chaoxing');
const busyRefreshing = ref(false);

const isSchoolSub = computed(() => ['exams', 'grades', 'credits'].includes(subTab.value));

// ==================== 超星学习通状态 ====================
const cxStatus = ref<any>({ linked: false, user: '', canRelink: false });
const cxScanState = ref<any>({ state: 'idle' });
const showCxLoginBox = ref(false);
const cxManualMode = ref(false);
const showCxPwd = ref(false);
const cxLoggingIn = ref(false);
const cxForm = ref({ user: '', pass: '' });
const workFilter = ref<'todo' | 'done' | 'all'>('todo');
const cxLiveLoading = ref(false);
const cxLiveResult = ref<any>(null);
const showCxNotices = ref(false);
const cxNoticesLoading = ref(false);
const cxNoticesList = ref<any[]>([]);
let cxPollTimer: number | undefined;

const forgotCxUrl = computed(
  () => store.meta?.forgotPwdUrls?.chaoxing || 'https://passport2.chaoxing.com/pwd/getpwd'
);

async function loadCxStatus() {
  if (!store.authenticated) return;
  const [res, todoRes] = await Promise.all([
    apiRequest('/api/v1/cx/status'),
    apiRequest('/api/v1/cx/todo'),
  ]);
  if (res?.ok) {
    cxStatus.value = res;
    cxScanState.value = res.scan || { state: 'idle' };
  }
  if (todoRes?.ok && todoRes.data) {
    store.cxTodo = todoRes.data;
  }
  if (cxScanState.value?.state === 'scanning') {
    startCxPolling();
  }
}

function fillCxDemo() {
  cxForm.value.user = 'demo';
  cxForm.value.pass = 'demo123';
}

async function submitCxLogin(useSaved: boolean) {
  if (!useSaved && (!cxForm.value.user || !cxForm.value.pass)) {
    store.showToast('请输入超星学习通账号和密码');
    return;
  }
  cxLoggingIn.value = true;
  try {
    const payload = useSaved ? { user: '', pass: '' } : { user: cxForm.value.user, pass: cxForm.value.pass };
    const res = await store.runTrackedTask(
      {
        title: '正在连接超星学习通',
        stage: useSaved ? '使用已保存加密凭据自动重连…' : '加密验证超星账号并建立会话…',
        kind: 'sync',
        doneStage: '学习通连接成功',
      },
      () => apiRequest('/api/v1/cx/login', payload)
    );
    if (res?.ok) {
      cxForm.value.pass = '';
      showCxLoginBox.value = false;
      cxManualMode.value = false;
      await Promise.all([loadCxStatus(), store.refreshSessionBriefs()]);
      await triggerCxScan(true);
      store.showToast('超星学习通已连接，正在后台扫描本学期课程作业');
    } else {
      if (res?.pwdStale) cxManualMode.value = true;
      store.showToast(res?.error || '学习通登录失败');
    }
  } finally {
    cxLoggingIn.value = false;
  }
}

async function triggerCxScan(force = false) {
  const res = await apiRequest('/api/v1/cx/scan', { force });
  if (res?.ok) {
    cxScanState.value = res.scan || { state: 'scanning' };
    startCxPolling();
  } else if (res?.needLogin) {
    showCxLoginBox.value = true;
  } else if (res?.error) {
    store.showToast(res.error);
  }
}

function startCxPolling() {
  if (cxPollTimer) window.clearInterval(cxPollTimer);
  let failCount = 0;
  cxPollTimer = window.setInterval(async () => {
    const res = await apiRequest('/api/v1/cx/todo', undefined, 'GET', { silentLog: true });
    if (res?.ok) {
      failCount = 0;
      cxScanState.value = res.scan || { state: 'idle' };
      if (res.data) store.cxTodo = res.data;
      if (cxScanState.value.state !== 'scanning') {
        window.clearInterval(cxPollTimer);
        cxPollTimer = undefined;
        if (cxScanState.value.state === 'done') {
          store.showToast('学习通作业与在线测验扫描完成');
        }
      }
    } else {
      failCount += 1;
      if (res?.needBind || failCount >= 3) {
        window.clearInterval(cxPollTimer);
        cxPollTimer = undefined;
        cxScanState.value = {
          state: 'error',
          err: res?.error || '获取扫描进度失败，请稍后重试',
        };
        store.showToast(res?.error || '学习通扫描轮询中断，请检查网络或登录状态');
      }
    }
  }, 1000);
}

async function runCxLiveCheck() {
  cxLiveLoading.value = true;
  try {
    const names = (store.timetable?.courses || []).map((c) => c.name);
    const res = await apiRequest('/api/v1/cx/live', { names });
    if (res?.ok) {
      cxLiveResult.value = res;
    } else {
      store.showToast(res?.error || '快查失败');
    }
  } finally {
    cxLiveLoading.value = false;
  }
}

async function openCxNoticeSheet() {
  showCxNotices.value = true;
  cxNoticesLoading.value = true;
  try {
    const res = await apiRequest('/api/v1/cx/notices');
    if (res?.ok && Array.isArray(res.list)) {
      cxNoticesList.value = res.list;
    } else {
      store.showToast(res?.error || '获取学习通收件箱失败');
      if (res?.needLogin) {
        showCxNotices.value = false;
        showCxLoginBox.value = true;
      }
    }
  } finally {
    cxNoticesLoading.value = false;
  }
}

function isMarkedDone(item: any): boolean {
  const dkey = `${item.course}|${item.title}`;
  const mk = store.preferences.doneMarks?.[dkey];
  if (!mk) return false;
  return !item.endAt || Math.abs(mk - Number(item.endAt)) < 7200;
}

async function toggleDoneMark(item: any) {
  const dkey = `${item.course}|${item.title}`;
  const nextMarks = { ...(store.preferences.doneMarks || {}) };
  if (isMarkedDone(item)) {
    delete nextMarks[dkey];
    store.showToast('已恢复为待完成');
  } else {
    nextMarks[dkey] = Number(item.endAt || Math.floor(Date.now() / 1000));
    store.showToast('已标记为完成（课表截止圆点同步消去）');
  }
  await store.updatePreferences({ doneMarks: nextMarks });
}

const cxFilteredCounts = computed(() => {
  const items = store.cxTodo?.items || [];
  let todo = 0;
  let done = 0;
  for (const it of items) {
    if (it.bucket === 'todo' && !isMarkedDone(it)) todo++;
    else done++;
  }
  return { todo, done };
});

const cxDisplayItems = computed(() => {
  const items = store.cxTodo?.items || [];
  if (workFilter.value === 'all') return items;
  if (workFilter.value === 'todo') {
    return items.filter((it: any) => it.bucket === 'todo' && !isMarkedDone(it));
  }
  return items.filter((it: any) => it.bucket !== 'todo' || isMarkedDone(it));
});

// ==================== 中国大学 MOOC 状态 ====================
const moocStatus = ref<any>({ linked: false, user: '' });
const showMoocBindBox = ref(false);
const moocCookieInput = ref('');
const moocBinding = ref(false);
const moocScanning = ref(false);

async function loadMoocStatus() {
  if (!store.authenticated) return;
  const res = await apiRequest('/api/v1/mooc/status');
  if (res?.ok) {
    moocStatus.value = res;
    if (res.data) store.moocTodo = res.data;
  }
}

function bindMoocDemo() {
  moocCookieInput.value = 'demo';
  submitMoocBind();
}

async function submitMoocBind() {
  if (!moocCookieInput.value) {
    store.showToast('请粘贴中国大学 MOOC 的 Cookie 字符串');
    return;
  }
  moocBinding.value = true;
  try {
    const res = await store.runTrackedTask(
      {
        title: '正在绑定并扫描中国大学 MOOC',
        stage: '校验 Cookie 凭据并拉取在修课程测验…',
        kind: 'sync',
        doneStage: 'MOOC 绑定与扫描完成',
      },
      () => apiRequest('/api/v1/mooc/bind', { cookie: moocCookieInput.value })
    );
    if (res?.ok) {
      moocCookieInput.value = '';
      showMoocBindBox.value = false;
      if (res.data) store.moocTodo = res.data;
      await Promise.all([loadMoocStatus(), store.refreshSessionBriefs()]);
      store.showToast('中国大学 MOOC 绑定并扫描成功');
    } else {
      store.showToast(res?.error || 'MOOC 绑定失败');
    }
  } finally {
    moocBinding.value = false;
  }
}

async function triggerMoocScan() {
  moocScanning.value = true;
  try {
    const res = await store.runTrackedTask(
      {
        title: '正在同步中国大学 MOOC 任务',
        stage: '扫描各章节单元测验与互评作业截止时间…',
        kind: 'sync',
        doneStage: 'MOOC 任务同步完成',
      },
      () => apiRequest('/api/v1/mooc/scan', {})
    );
    if (res?.ok && res.data) {
      store.moocTodo = res.data;
      store.showToast('已更新 MOOC 课程测验与作业');
    } else {
      store.showToast(res?.error || '扫描失败');
    }
  } finally {
    moocScanning.value = false;
  }
}

const moocDisplayItems = computed(() => store.moocTodo?.items || []);

function formatDeadline(sec: number): string {
  const dt = new Date(sec * 1000);
  return `${dt.getMonth() + 1}月${dt.getDate()}日 ${String(dt.getHours()).padStart(2, '0')}:${String(dt.getMinutes()).padStart(2, '0')} 截止`;
}

// ==================== 预留平台（雨课堂 / 智慧树 / 知到） ====================
const extState = ref<any>({ linked: false, account: '', portalUrl: '', note: '' });
const extForm = ref({ account: '', secret: '', memo: '' });
const showExtSecret = ref(false);
const extSaving = ref(false);

const activeReservedMeta = computed(() => {
  const map: Record<string, { name: string; homeUrl: string; desc: string }> = {
    yuketang: {
      name: '雨课堂（长江雨课堂）',
      homeUrl: 'https://changjiang.yuketang.cn',
      desc: '海南大学公共课与理工科课堂常用互动平台，支持在此登记账号备忘并一键直达网页端。',
    },
    zhihuishu: {
      name: '智慧树在线学堂',
      homeUrl: 'https://onlineweb.zhihuishu.com',
      desc: '海南大学通识教育跨校共享课平台，支持加密登记账号与选课备忘并一键直达在线学堂。',
    },
    zhidao: {
      name: '知到（智慧树移动版入口）',
      homeUrl: 'https://www.zhihuishu.com',
      desc: '与智慧树学分课账号互通，预留标准化插件接口槽位与快捷直达入口。',
    },
  };
  return map[subTab.value] || map.yuketang;
});

async function loadReservedStatus(plat: string) {
  if (!store.authenticated) return;
  const res = await apiRequest(`/api/v1/ext/${plat}/status`);
  if (subTab.value !== plat) return;
  if (res?.ok) {
    extState.value = res;
    extForm.value = { account: '', secret: '', memo: res.note || '' };
  }
}

async function saveReservedPlatform() {
  const plat = subTab.value;
  if (!extState.value.linked && !extForm.value.account) {
    store.showToast('请填写该平台的绑定手机号或学号');
    return;
  }
  extSaving.value = true;
  try {
    const res = await store.runTrackedTask(
      {
        title: `正在加密保存 ${activeReservedMeta.value.name}`,
        stage: '上传至 AES-256-GCM 凭据保险箱…',
        kind: 'upload',
        doneStage: '平台账号备忘已保存',
      },
      () => apiRequest(`/api/v1/ext/${plat}/bind`, extForm.value)
    );
    if (res?.ok) {
      extForm.value.secret = '';
      await Promise.all([loadReservedStatus(plat), store.refreshSessionBriefs()]);
      store.showToast(`已加密保存 ${activeReservedMeta.value.name} 账号信息`);
    } else {
      store.showToast(res?.error || '保存失败');
    }
  } finally {
    extSaving.value = false;
  }
}

// ==================== 学校平台：考试 / 成绩 / 学分 ====================
const activeExamSem = ref('');
const customExamData = ref<any>(null);
const jwTodoLoading = ref(false);
const jwTodoItems = ref<any[]>([]);

const examViewData = computed(() => customExamData.value || store.exams);

async function loadExamsForSem(semId: string) {
  const res = await store.runTrackedTask(
    {
      title: `正在同步 ${semId} 排考信息`,
      stage: '拉取教务考场教室与座位号…',
      kind: 'sync',
      doneStage: '考场安排已同步',
    },
    () => apiRequest('/api/v1/jw/exams', { semesterId: semId })
  );
  if (res?.ok) {
    activeExamSem.value = semId;
    customExamData.value = res;
  } else {
    store.showToast(res?.error || '读取该学期排考失败');
  }
}

async function probeJwTodo() {
  jwTodoLoading.value = true;
  try {
    const res = await store.runTrackedTask(
      {
        title: '正在探测教务实时通道',
        stage: '检查选课轮次与期末评教开放状态…',
        kind: 'sync',
        doneStage: '教务通道探测完成',
      },
      () => apiRequest('/api/v1/jw/todo')
    );
    if (res?.ok && Array.isArray(res.items)) {
      jwTodoItems.value = res.items;
    } else {
      store.showToast(res?.error || '探测失败');
    }
  } finally {
    jwTodoLoading.value = false;
  }
}

function examDaysDiff(ymd: string): number {
  if (!ymd) return 999;
  const [y, m, d] = ymd.replace(/\//g, '-').slice(0, 10).split('-').map(Number);
  if (!y || !m || !d) return 999;
  const target = new Date(y, m - 1, d);
  const now = new Date();
  const today0 = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  return Math.round((target.getTime() - today0.getTime()) / 86400000);
}

function formatExamCountdown(ymd: string): string {
  const diff = examDaysDiff(ymd);
  if (diff === 999) return '待定';
  if (diff < 0) return '已结束';
  if (diff === 0) return '今天开考';
  return `还有 ${diff} 天`;
}

const gradeTermFilter = ref('');
const gradeKeyword = ref('');
const activeGradeSem = ref('');
const gradeStageFilter = ref<'all' | 'midterm' | 'final' | 'overall'>('all');
const customGradesData = ref<any>(null);
const gradesQuerying = ref(false);
const gradesAutoQueried = ref(false);

const GRADE_STAGES: Array<{ key: 'all' | 'midterm' | 'final' | 'overall'; label: string }> = [
  { key: 'all', label: '全部' },
  { key: 'midterm', label: '期中成绩' },
  { key: 'final', label: '期末成绩' },
  { key: 'overall', label: '总评已出' },
];

const gradeViewData = computed<any>(() => customGradesData.value || store.grades);

const gradeSemChips = computed(() => {
  const cur = store.timetable?.semesterId || '';
  const ids = new Set<string>();
  for (const s of store.semesters || []) {
    if (!cur || s.id <= cur) ids.add(s.id);
  }
  if (cur) ids.add(cur);
  for (const c of gradeViewData.value?.courses || []) {
    if (c.term) ids.add(c.term);
  }
  return Array.from(ids).sort().reverse().slice(0, 8);
});

const gradeTermOptions = computed(() => {
  const set = new Set<string>();
  for (const c of gradeViewData.value?.courses || []) {
    if (c.term) set.add(c.term);
  }
  return Array.from(set).sort().reverse();
});

const gradeStageCounts = computed(() => {
  const list: any[] = gradeViewData.value?.courses || [];
  return {
    all: list.length,
    midterm: list.filter((c) => !!c.midtermScore).length,
    final: list.filter((c) => !!c.finalScore || !!c.score).length,
    overall: list.filter((c) => !!c.score).length,
  };
});

const filteredGrades = computed(() => {
  const kw = gradeKeywordLower();
  return (gradeViewData.value?.courses || []).filter((c: any) => {
    if (gradeTermFilter.value && c.term !== gradeTermFilter.value) return false;
    if (gradeStageFilter.value === 'midterm' && !c.midtermScore) return false;
    if (gradeStageFilter.value === 'final' && !c.finalScore && !c.score) return false;
    if (gradeStageFilter.value === 'overall' && !c.score) return false;
    if (kw && !(c.name || '').toLowerCase().includes(kw) && !(c.category || '').toLowerCase().includes(kw)) {
      return false;
    }
    return true;
  });
});

function gradeKeywordLower(): string {
  return gradeKeyword.value.toLowerCase();
}

// 仅统计已出总评且含有效绩点的课程（排除缓考/免修及空绩点课程，避免被当作 0.0 拉低均绩）
const computedGpaSummary = computed(() => {
  let pts = 0;
  let creds = 0;
  for (const c of gradeViewData.value?.courses || []) {
    if (!c.score) continue;
    const flag = String(c.scoreFlag || '');
    if (flag.includes('缓考') || flag.includes('免修')) continue;
    const rawGpa = c.gpa !== undefined && c.gpa !== null ? String(c.gpa).trim() : '';
    if (!rawGpa) continue;
    const cr = parseFloat(c.credit || '0');
    const gp = parseFloat(rawGpa);
    if (cr > 0 && !Number.isNaN(gp)) {
      pts += cr * gp;
      creds += cr;
    }
  }
  return {
    credits: creds.toFixed(1),
    gpa: creds > 0 ? (pts / creds).toFixed(2) : '—',
  };
});

async function queryGradesForSem(semId: string = activeGradeSem.value) {
  if (gradesQuerying.value) return;
  gradesQuerying.value = true;
  const syncId = store.createSyncId('grades');
  store.startProgressTask({
    title: `正在查询${semId || '全部学期'}成绩`,
    stage: '连接教务成绩系统…',
    detail: '期中 · 期末 · 平时 · 实验 · 总评',
    totalSteps: 6,
    kind: 'sync',
    pollSyncId: syncId,
    initialPercent: 8,
    autoCreepTo: 90,
  });
  try {
    const res = await apiRequest('/api/v1/jw/grades', { semesterId: semId, syncId });
    if (res?.ok) {
      activeGradeSem.value = semId;
      const courses: any[] = Array.isArray(res.courses) ? res.courses : [];
      if (!semId) {
        customGradesData.value = null;
        store.grades = res as any;
      } else {
        customGradesData.value = res;
        if (courses.length > 0 && !(store.grades?.courses || []).length) store.grades = res as any;
      }
      gradeTermFilter.value = '';
      const mid = courses.filter((c) => !!c.midtermScore).length;
      const fin = courses.filter((c) => !!c.finalScore || !!c.score).length;
      let msg: string;
      if (res.fromCache) {
        msg = `教务暂时无法连接，已显示本地缓存成绩${res.staleWarn ? `（${res.staleWarn}）` : ''}`;
      } else if (res.evalBlocked) {
        msg = '教务系统要求先完成教学评价后才能查看成绩';
      } else if (courses.length === 0) {
        msg = res.notice || `${semId || '全部学期'}暂未发布成绩`;
      } else {
        msg = `已查询：共 ${courses.length} 门（期中 ${mid} 门 · 期末/总评 ${fin} 门）`;
      }
      store.finishProgressTask(true, courses.length ? '成绩查询完成' : '教务暂无已发布成绩', msg);
      store.showToast(msg);
    } else if (res?.needCaptcha) {
      store.credState = 'expired';
      if (res.captcha?.ok) store.latestCaptcha = res.captcha;
      store.finishProgressTask(false, '教务单次会话已过期', '已保留本地缓存，输一次验证码即可恢复查询');
      store.showToast('教务单次会话已过期，请输入一次验证码恢复查询');
      store.switchTab('me');
    } else {
      store.finishProgressTask(false, res?.kind === 'net' ? '教务网络连接超时' : '成绩查询未完成', res?.error || '');
      store.showToast(res?.error || '成绩查询失败');
    }
  } finally {
    gradesQuerying.value = false;
  }
}

const creditFilter = ref<'all' | 'pending' | 'studying' | 'passed'>('all');
const showGradTargetInput = ref(false);
const customGradTargetInput = ref<number | undefined>(undefined);

const gradAudit = computed(() => store.credits?.graduationAudit || null);

async function saveCustomGradTarget() {
  const val = Number(customGradTargetInput.value || 0);
  if (!Number.isFinite(val) || val < 100 || val > 240) {
    store.showToast('请输入 100 ~ 240 之间的有效毕业总学分要求');
    return;
  }
  const nextRemind = { ...(store.preferences.remindCfg || { courseBeforeMin: 15, workBeforeHours: 6, enabled: false }), gradTargetCredits: val };
  await store.updatePreferences({ remindCfg: nextRemind });
  const sessRes = await apiRequest('/api/v1/auth/session');
  if (sessRes?.ok && sessRes.authenticated) {
    store.applyBundle(sessRes);
  }
  showGradTargetInput.value = false;
  store.showToast(`已更新毕业应修总学分目标为 ${val} 学分`);
}

const filteredPlanCourses = computed(() => {
  const list = store.credits?.courses || [];
  if (creditFilter.value === 'all') return list;
  return list.filter((c) => c.state === creditFilter.value);
});

// ==================== 子页切换与刷新调度 ====================
function switchSubTab(next: SubTabKey) {
  subTab.value = next;
  if (next === 'chaoxing') loadCxStatus();
  else if (next === 'mooc') loadMoocStatus();
  else if (next === 'yuketang' || next === 'zhihuishu' || next === 'zhidao') {
    extState.value = { linked: false, account: '', portalUrl: '', note: '' };
    extForm.value = { account: '', secret: '', memo: '' };
    loadReservedStatus(next);
  } else if (next === 'exams' && !activeExamSem.value && store.exams?.semesterId) {
    activeExamSem.value = store.exams.semesterId;
  } else if (
    next === 'grades' &&
    store.authenticated &&
    store.credState === 'ok' &&
    !gradesAutoQueried.value &&
    (gradeViewData.value?.courses || []).length === 0
  ) {
    gradesAutoQueried.value = true;
    queryGradesForSem(activeGradeSem.value);
  }
}

async function refreshCurrentSubTab() {
  if (!store.authenticated) return;
  busyRefreshing.value = true;
  try {
    if (subTab.value === 'chaoxing') {
      await loadCxStatus();
      if (cxStatus.value.linked) await triggerCxScan(true);
    } else if (subTab.value === 'mooc') {
      await loadMoocStatus();
      if (moocStatus.value.linked) await triggerMoocScan();
    } else if (subTab.value === 'grades') {
      await queryGradesForSem(activeGradeSem.value);
    } else if (subTab.value === 'exams') {
      if (customExamData.value && activeExamSem.value) {
        await loadExamsForSem(activeExamSem.value);
      } else {
        await store.syncJwxt();
        customExamData.value = null;
      }
    } else if (isSchoolSub.value) {
      await store.syncJwxt();
    } else {
      await loadReservedStatus(subTab.value);
    }
  } finally {
    busyRefreshing.value = false;
  }
}

watch(
  [() => store.authenticated, () => store.profile?.studentId],
  ([authed, sid], [, prevSid]) => {
    if (!authed || (prevSid && sid && sid !== prevSid)) {
      gradesAutoQueried.value = false;
      customGradesData.value = null;
      customExamData.value = null;
      activeExamSem.value = '';
      activeGradeSem.value = '';
      jwTodoItems.value = [];
      cxStatus.value = { linked: false, user: '', canRelink: false };
      cxScanState.value = { state: 'idle' };
      cxLiveResult.value = null;
      cxNoticesList.value = [];
      moocStatus.value = { linked: false, user: '' };
      extState.value = { linked: false, account: '', portalUrl: '', note: '' };
    }
    if (authed) {
      switchSubTab(subTab.value);
    }
  }
);

onMounted(() => {
  if (store.studyTargetSub) {
    const target = store.studyTargetSub as SubTabKey;
    store.studyTargetSub = '';
    switchSubTab(target);
  } else {
    loadCxStatus();
  }
});

onUnmounted(() => {
  if (cxPollTimer) window.clearInterval(cxPollTimer);
});
</script>

<style scoped>
.study-root {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
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

.saved-hint-box {
  padding: 10px 12px;
  border-radius: var(--r-list);
  background: var(--fill);
  font-size: 13px;
}

.scan-progress-box {
  margin-top: 10px;
  padding: 10px 12px;
  border-radius: var(--r-list);
  background: var(--fill);
}

.scan-err-box {
  margin-top: 10px;
  padding: 10px 12px;
  border-radius: var(--r-list);
  background: var(--danger-bg);
  color: var(--danger);
  font-size: 12px;
}

.progress-track {
  height: 7px;
  border-radius: 999px;
  background: var(--fill-strong);
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: var(--accent);
  border-radius: 999px;
  transition: width 220ms ease;
}

.progress-fill.animated-fill {
  position: relative;
  overflow: hidden;
  background: linear-gradient(90deg, var(--accent), #38bdf8);
}

.progress-fill.animated-fill::after {
  content: "";
  position: absolute;
  inset: 0;
  background: linear-gradient(110deg, transparent 20%, rgba(255, 255, 255, 0.45) 50%, transparent 80%);
  background-size: 200% 100%;
  animation: study-prog-shimmer 1.2s linear infinite;
}

@keyframes study-prog-shimmer {
  0% { background-position: 200% 0; }
  100% { background-position: -200% 0; }
}

.action-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.sub-filter-row {
  display: flex;
  gap: 8px;
  overflow-x: auto;
  margin-bottom: 10px;
  padding-bottom: 2px;
}

.kind-tag {
  display: inline-block;
  font-size: 11px;
  padding: 1px 6px;
  border-radius: 4px;
  margin-right: 6px;
  background: var(--fill);
  color: var(--due-work);
}

.kind-tag.exam {
  background: var(--due-exam-bg);
  color: var(--due-exam);
}

.done-toggle-btn {
  padding: 5px 10px;
  border-radius: var(--r-chip);
  font-size: 12px;
  font-weight: 600;
  background: var(--fill);
  color: var(--text);
}

.done-toggle-btn.marked {
  background: rgba(52, 199, 89, 0.16);
  color: var(--ok);
}

.live-act-item {
  margin-top: 8px;
  padding: 8px 10px;
  border-radius: var(--r-list);
  background: var(--fill);
  font-size: 13px;
}

.channel-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 6px 0;
  border-top: 0.5px solid var(--sep);
}

.exam-cd-badge {
  padding: 4px 10px;
  border-radius: 999px;
  background: var(--fill);
  font-size: 12px;
  font-weight: 600;
}

.exam-cd-badge.urgent {
  background: var(--due-exam-bg);
  color: var(--due-exam);
}

.stats-3-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
  text-align: center;
}

.stat-label {
  font-size: 12px;
  color: var(--sub);
}

.stat-num {
  font-size: 24px;
  font-weight: 700;
  margin-top: 4px;
}

.stat-num.accent {
  color: var(--accent);
}

.gpa-bars {
  display: flex;
  align-items: flex-end;
  justify-content: space-around;
  gap: 10px;
  height: 118px;
  padding-top: 12px;
}

.gpa-bar-col {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  height: 100%;
}

.gpa-val {
  font-size: 11px;
  font-weight: 700;
  margin-bottom: 4px;
}

.gpa-bar-track {
  flex: 1;
  width: 24px;
  border-radius: 6px;
  background: var(--fill);
  display: flex;
  align-items: flex-end;
  overflow: hidden;
}

.gpa-bar-fill {
  width: 100%;
  background: var(--accent);
  border-radius: 6px;
}

.gpa-term {
  font-size: 10px;
  color: var(--sub);
  margin-top: 4px;
}

.grade-query-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.grade-toolbar {
  display: flex;
  gap: 8px;
  margin-bottom: 10px;
}

.grade-right {
  text-align: right;
  flex-shrink: 0;
}

.score-main {
  font-size: 17px;
  font-weight: 700;
  color: var(--accent);
}

.cal-big-week {
  font-size: 24px;
  font-weight: 700;
  margin-top: 2px;
}

.cal-sub-total {
  font-size: 13px;
  font-weight: 400;
  color: var(--sub);
}

.seat-badge {
  padding: 4px 10px;
  border-radius: 999px;
  background: var(--accent-soft);
  color: var(--accent);
  font-size: 12px;
  font-weight: 600;
}

.mod-row + .mod-row {
  margin-top: 14px;
}

.grade-card-row {
  align-items: stretch;
}

.grade-title-line {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 5px;
}

.grade-pill {
  display: inline-block;
  padding: 1px 6px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 600;
  background: var(--fill);
  color: var(--sub);
}

.grade-pill.req {
  background: var(--accent-soft);
  color: var(--accent);
}

.grade-pill.elec {
  background: rgba(52, 199, 89, 0.14);
  color: var(--ok);
}

.grade-pill.warn {
  background: var(--warn-bg);
  color: var(--warn);
}

.subscore-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(72px, 1fr));
  gap: 6px;
  margin-top: 8px;
  padding: 7px 10px;
  border-radius: var(--r-list);
  background: var(--fill);
}

.subscore-cell {
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.ss-label {
  font-size: 11px;
  color: var(--sub);
}

.ss-val {
  font-size: 13px;
  font-weight: 700;
  color: var(--text);
}

.dual-track {
  position: relative;
}

.dual-track .progress-fill {
  position: relative;
  z-index: 2;
}

.dual-track .progress-fill.studying-layer {
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  z-index: 1;
  opacity: 0.35;
}

.audit-warn-box {
  margin-top: 10px;
  padding: 10px 12px;
  border-radius: var(--r-list);
  background: var(--accent-soft);
  color: var(--text);
  font-size: 12px;
  line-height: 1.48;
}

.grad-target-editor {
  display: flex;
  gap: 8px;
  margin-top: 10px;
}

.grad-ref-summary {
  margin-top: 12px;
  padding: 10px 12px;
  border-radius: var(--r-list);
  background: var(--fill);
}

.elective-audit-list {
  margin-top: 10px;
}

.elec-audit-item {
  padding: 10px 0;
  border-top: 0.5px solid var(--sep);
}

.elec-stat {
  font-size: 12px;
  font-weight: 600;
  color: var(--accent);
}
</style>
