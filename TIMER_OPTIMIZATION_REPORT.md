# ��ʱ�������Ż�����

## �������

ԭʼ������������������⣺
1. **��ʱ�����Ȳ���**��ʹ��5ms�ı�׼Windows��ʱ������ĳЩϵͳ�Ͽ��ܵ��¿���
2. **�̶�ˢ����**��û�и�����ʾ��ˢ�����Ż���������
3. **�ػ�Ƶ�ʹ���**������ƶ�ʱû�������ػ�Ƶ�ʣ����²���Ҫ����������

## �Ż�����

### 1. �߾��ȶ�ý�嶨ʱ��֧��

#### ʵ��ԭ��
- **multimedia timer API**��ʹ��`timeSetEvent`�����׼`SetTimer`
- **��������**����~15ms���͵�1-2ms�Ķ�ʱ������
- **�Զ�����**�������ý�嶨ʱ�������ã��Զ����˵���׼��ʱ��

#### �ؼ�����
```cpp
// ��ʼ���߾��ȶ�ʱ��
void initializeHighPrecisionTimer() {
    TIMECAPS timeCaps;
    if (timeGetDevCaps(&timeCaps, sizeof(TIMECAPS)) == MMSYSERR_NOERROR) {
        UINT period = std::max(1U, timeCaps.wPeriodMin);
        if (timeBeginPeriod(period) == MMSYSERR_NOERROR) {
            mmTimerPeriodSet_ = true;
            useHighPrecisionTimer_ = true;
        }
    }
}

// �߾��ȶ�����ʱ��
mmTimerId_ = timeSetEvent(8, 1, mmTimerCallback, (DWORD_PTR)this, TIME_PERIODIC);
```

### 2. ����Ӧˢ�����Ż�

#### ��ʾ��ˢ���ʼ��
- **��̬���**����ȡ��ǰ��ʾ����ʵ��ˢ����
- **����Ӧ���**������ˢ���ʵ�����ʱ�����
  - 120Hz+ ��ʾ����4ms��� (250fps)
  - 90Hz+ ��ʾ����6ms��� (167fps)  
  - 60Hz ��ʾ����8ms��� (125fps)

#### �ؼ�����
```cpp
void initializeAnimationSystem() {
    displayRefreshRate_ = getDisplayRefreshRate();
    
    if (displayRefreshRate_ >= 120) {
        fadeInterval_ = 4;  // 4ms = 250Hz for 120Hz+ displays
    } else if (displayRefreshRate_ >= 90) {
        fadeInterval_ = 6;  // 6ms = 167Hz for 90Hz+ displays  
    } else {
        fadeInterval_ = 8;  // 8ms = 125Hz for 60Hz displays
    }
}
```

### 3. �����Ż�

#### ����Ƶ������
- **����������**����������ƶ��Ͷ������µ���С���
- **�����ػ�**��ֻ�ڱ�Ҫʱ�����ػ棬����GPU����

#### �ؼ�����
```cpp
void updateFade() {
    // �����Ż������Ƹ���Ƶ�ʣ���������ػ�
    DWORD currentTime = GetTickCount();
    if (currentTime - lastUpdateTime_ < MIN_UPDATE_INTERVAL) {
        return; // ������θ���
    }
    lastUpdateTime_ = currentTime;
    // ...�����߼�
}

void updateSelect(int x, int y) {
    // �����Ż�����������ƶ�����Ƶ��
    DWORD currentTime = GetTickCount();
    if (currentTime - lastUpdateTime_ < MIN_UPDATE_INTERVAL) {
        return; // ������θ��£������ػ�Ƶ��
    }
    // ...ѡ���߼�
}
```

#### �����㷨�Ż�
- **���̶���ʱ��**����350ms���ٵ�250ms�����ٿ��ٸ�֪
- **�Ľ���������**��ʹ��cubic ease-in-out���quadratic������Ȼ�Ķ���Ч��
- **�����ػ�����**����30px�߽�������20px�������ػ����

## ��������Ч��

### ��������
1. **��ʱ������**����~15ms������1-2ms������7-15����
2. **����������**��
   - 60Hz��ʾ������~67fps������125fps
   - 120Hz��ʾ����֧�ָߴ�250fps�Ķ�������
   - 144Hz��ʾ����֧�ָߴ�250fps�Ķ�������

### ʵ��Ч��
- **���ټ���**���߾��ȶ�ʱ��������ϵͳ��ʱ���Ĳ��ȶ���
- **��Ӧ������**����������ƽ�����ر����ڸ�ˢ������ʾ����
- **CPU�Ż�**��ͨ������Ƶ�����ƣ����ٲ���Ҫ�ļ�����ػ�

## ������

### ϵͳ������
- **Windows 7+**����ȫ֧�ֶ�ý�嶨ʱ��
- **��ϵͳ**���Զ����˵���׼��ʱ�������ּ�����
- **����ʾ��**��֧�ֲ�ͬˢ���ʵĶ���ʾ������

### ��Դ����
- **�Զ�����**����������������ȷ�������ж�ʱ����Դ
- **������**�������Ĵ��������־��¼
- **�ڴ氲ȫ**��ʹ��RAIIԭ�������Դ

## ʹ�ý���

1. **�Ƽ�����**����֧�ֵ�ϵͳ�����ø߾��ȶ�ʱ���Ի���������
2. **���ܼ��**��ͨ����־�鿴ʵ��ʹ�õĶ�ʱ�����ͺ�ˢ����
3. **����ģʽ**������ͨ������`MIN_UPDATE_INTERVAL`������ƽ�����ܺ�������

��Щ�Ż�Ӧ�����������������Ŀ������⣬�ر����ڶ������ź������Ӧ���档