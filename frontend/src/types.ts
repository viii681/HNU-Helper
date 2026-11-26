export interface CourseSlot {
  day: number;     // 0 = 周一 ... 6 = 周日
  period: number;  // 0 = 第1节 ... 10 = 第11节
}

export interface CourseItem {
  name: string;
  code: string;
  teacher: string;
  room: string;
  weeks: number[];
  slots: CourseSlot[];
  isCustom?: boolean;
}

export interface TimetableData {
  semesterId: string;
  semester: string;
  unitCount: number;
  totalWeeks: number;
  courses: CourseItem[];
}

export interface GradeCourse {
  term: string;
  code: string;
  seq: string;
  name: string;
  category: string;
  nature: string;
  generalCategory?: string;
  credit: string;
  hours?: string;
  score: string;
  regularScore?: string;
  regularRatio?: string;
  midtermScore?: string;
  midtermRatio?: string;
  finalScore?: string;
  finalRatio?: string;
  labScore?: string;
  labRatio?: string;
  final: string;
  gpa: string;
  scoreFlag?: string;
  makeupTerm?: string;
  examMethod?: string;
  examNature?: string;
}

export interface GradesData {
  overall: { count: string; credits: string; gpa: string } | null;
  summary: Array<{ year: string; term: string; semester?: string; count: string; credits: string; gpa: string }>;
  courses: GradeCourse[];
  error?: string;
  midtermCount?: number;
  finalCount?: number;
  regularCount?: number;
  evalBlocked?: boolean;
  notice?: string;
  queriedSemester?: string;
  sourcesChecked?: Array<{ path: string; label: string; count: number }>;
}

export interface ExamItem {
  code: string;
  name: string;
  category: string;
  form: string;
  weekText: string;
  date: string;
  arrange: string;
  campus?: string;
  room: string;
  seat: string;
  status: string;
}

export interface ExamsData {
  semesterId?: string;
  published: boolean;
  exams: ExamItem[];
  semesters?: Array<{ id: string; name: string }>;
  error?: string;
}

export interface GraduationAudit {
  matchedMajor: string;
  exactMatched: boolean;
  customTarget?: number;
  gradTotalRequired: number;
  electiveTotalRequired: number;
  generalElectiveRequired: number;
  generalElectiveEarned: number;
  generalElectiveStudying: number;
  generalElectiveRemain: number;
  generalElectiveRule: string;
  majorElectiveRequired: number;
  majorElectiveEarned: number;
  majorElectiveStudying: number;
  majorElectiveRemain: number;
  secondClassRequired: number;
  unscheduledCompulsoryEst: number;
  gradRemainAfterEarned: number;
  gradRemainAfterStudying: number;
  gradPercentWithStudying: number;
}

export interface CreditAnalysis {
  ready: boolean;
  error?: string;
  planScope?: 'graduation' | 'execution_plan';
  isGraduationPlan?: boolean;
  scopeTitle?: string;
  scopeNote?: string;
  totalRequired?: number;
  planRequired?: number;
  totalEarned?: number;
  totalStudying?: number;
  remainAfterEarned?: number;
  totalRemain?: number;
  percent?: number;
  percentWithStudying?: number;
  filteredIntlCredits?: number;
  dedupSubCredits?: number;
  graduationAudit?: GraduationAudit;
  modules?: Array<{
    name: string;
    required: number;
    earned: number;
    studying: number;
    remain: number;
    remainAfterEarned?: number;
    percent: number;
    percentWithStudying?: number;
  }>;
  courses?: Array<{
    code: string;
    name: string;
    credit: number;
    cat: string;
    required: boolean;
    terms: number[];
    termLabel?: string;
    state: 'passed' | 'studying' | 'pending';
    score: string;
    extraElective?: boolean;
  }>;
}

export interface UserProfile {
  studentId: string;
  name: string;
  gender: string;
  dept: string;
  college: string;
  academy: string;
  major: string;
  direction: string;
  className: string;
  campus: string;
  grade: string;
  level: string;
  duration: string;
  status: string;
  enrolledAt: string;
  graduateAt: string;
}

export interface UserPreferences {
  campusPreset: string;
  termStartDate: string;
  totalWeeks: number;
  bellSchedule: string[][];
  hiddenCourses: string[];
  hiddenSlots: Array<{ code: string; name: string; day: number; period: number }>;
  customCourses: CourseItem[];
  courseColors: Record<string, string>;
  doneMarks: Record<string, number>;
  seenItems: Record<string, number>;
  cardLowThreshold: number;
  elecLowThreshold: number;
  remindCfg: {
    courseBeforeMin: number;
    workBeforeHours: number;
    enabled: boolean;
    gradTargetCredits?: number;
  };
  telemetryOptIn: boolean;
}

export interface DormElecData {
  configured: boolean;
  campus: string;
  academy: string;
  building: string;
  room: string;
  meterAccount: string;
  balanceKwh: number | null;
  estimatedNowKwh: number | null;
  estimatedDaysLeft: number | null;
  dailyBurnKwh: number;
  recordedAt: number;
  payUrl: string;
}

export interface NoticeItem {
  title: string;
  url: string;
  source: string;
  dept: string;
  date: string;
  tier: 'impact' | 'action' | 'activity' | 'other';
  tags: string[];
}

export interface CampusAcademyGroup {
  campusId: string;
  campusName: string;
  academies: string[];
}

export interface ProgressTaskState {
  active: boolean;
  id: string;
  title: string;
  stage: string;
  detail: string;
  percent: number;
  step: number;
  totalSteps: number;
  status: 'running' | 'done' | 'error';
  startedAt: number;
  elapsedSec: number;
  kind: 'sync' | 'upload' | 'cache' | 'scan';
}

export type MainTab = 'schedule' | 'study' | 'wiki' | 'campus' | 'me';
export type SubPage = '' | 'calendar' | 'notice' | 'services' | 'accounts' | 'feedback';
