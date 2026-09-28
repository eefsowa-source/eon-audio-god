# EON_GOD

- `README.md`가 품질 모드·빌드·검증의 기준이다. 구현은 `Source/`, 순수 DSP 검사는 `Docs/`, 측정 결과는 `measurements/`에 있다.
- 이 저장소에는 `.codegraph/`가 있다. 코드 위치와 호출 관계를 찾을 때 먼저 `codegraph explore`를 사용한다. `JUCE/`는 별도 의존성 저장소다.
- DSP 변경 뒤 `ctest --test-dir <빌드 디렉터리> --output-on-failure`와 관련 측정 대상을 확인한다. 포맷 검사는 `scripts/run_pluginval.sh`로 별도 수행한다.
- 빌드가 플러그인을 사용자 설치 위치에 복사할 수 있다. 현재 설치 바이너리 해시와 호스트·UI·청취 증거를 빌드 결과와 구분한다.
