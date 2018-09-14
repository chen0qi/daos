pipeline {
    agent none

    environment {
        SHELL = '/bin/bash'
    }

    // triggers {
        // Jenkins instances behind firewalls can't get webhooks
        // sadly, this doesn't seem to work
        // pollSCM('* * * * *')
        // See if cron works since pollSCM above isn't
        // cron('H 22 * * *')
    // }

    options {
        // preserve stashes so that jobs can be started at the test stage
        preserveStashes(buildCount: 5)
    }

    stages {
        stage('Pre-build') {
            parallel {
                stage('check_modules.sh') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos:7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs  '--build-arg NOBUILD=1 --build-arg UID=$(id -u)'
                        }
                    }
                    steps {
                        githubNotify description: 'checkmodules.sh',  context: 'checkmodules.sh', status: 'PENDING'
                        sh '''git submodule update --init --recursive
                              utils/check_modules.sh'''
                    }
                    post {
                        success {
                            githubNotify description: 'checkmodules.sh',  context: 'checkmodules.sh', status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify description: 'checkmodules.sh',  context: 'checkmodules.sh', status: 'FAILURE'
                        }
                        failure {
                            githubNotify description: 'checkmodules.sh',  context: 'checkmodules.sh', status: 'ERROR'
                        }
                        always {
                            archiveArtifacts artifacts: 'pylint.log', allowEmptyArchive: true
                        }
                    }
                }
            }
        }
        stage('Build') {
            parallel {
                stage('Build on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos:7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs  '--build-arg NOBUILD=1 --build-arg UID=$(id -u)'
                        }
                    }
                    steps {
                        githubNotify description: 'CentOS 7 Build',  context: 'build/centos7', status: 'PENDING'
                        checkout scm
                        sh '''git submodule update --init --recursive
                              if git show -s --format=%B | grep "^Skip-build: true"; then
                                  exit 0
                              fi
                              scons -c
                              # scons -c is not perfect so get out the big hammer
                              rm -rf _build.external install build
                              utils/fetch_go_packages.sh -i .
                              SCONS_ARGS="--update-prereq=all --build-deps=yes USE_INSTALLED=all install"
                              if ! scons $SCONS_ARGS; then
                                  if ! scons --config=force $SCONS_ARGS; then
                                      rc=\${PIPESTATUS[0]}
                                      cat config.log || true
                                      exit \$rc
                                  fi
                              fi'''
                        stash name: 'CentOS-install', includes: 'install/**'
                        stash name: 'CentOS-build-vars', includes: '.build_vars.*'
                        stash name: 'CentOS-tests', includes: 'build/src/rdb/raft/src/tests_main, build/src/common/tests/btree_direct, build/src/common/tests/btree, src/common/tests/btree.sh, build/src/common/tests/sched, build/src/client/api/tests/eq_tests, src/vos/tests/evt_ctl.sh, build/src/vos/vea/tests/vea_ut, src/rdb/raft_tests/raft_tests.py'
                    }
                    post {
                        success {
                            githubNotify description: 'CentOS 7 Build',  context: 'build/centos7', status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify description: 'CentOS 7 Build',  context: 'build/centos7', status: 'FAILURE'
                        }
                        failure {
                            githubNotify description: 'CentOS 7 Build',  context: 'build/centos7', status: 'ERROR'
                        }
                    }
                }
                stage('Build on Ubuntu 18.04') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu:18.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs  '--build-arg NOBUILD=1 --build-arg UID=$(id -u) --build-arg DONT_USE_RPMS=false'
                        }
                    }
                    steps {
                        githubNotify description: 'Ubuntu 18 Build',  context: 'build/ubuntu18', status: 'PENDING'
                        checkout scm
                        sh '''git submodule update --init --recursive
                              if git show -s --format=%B | grep "^Skip-build: true"; then
                                  exit 0
                              fi
                              scons -c
                              # scons -c is not perfect so get out the big hammer
                              rm -rf _build.external install build
                              utils/fetch_go_packages.sh -i .
                              SCONS_ARGS="--update-prereq=all --build-deps=yes USE_INSTALLED=all install"
                              if ! scons $SCONS_ARGS; then
                                  if ! scons --config=force $SCONS_ARGS; then
                                      rc=\${PIPESTATUS[0]}
                                      cat config.log || true
                                      exit \$rc
                                  fi
                              fi'''
                    }
                    post {
                        success {
                            githubNotify description: 'Ubuntu 18 Build',  context: 'build/ubuntu18', status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify description: 'Ubuntu 18 Build',  context: 'build/ubuntu18', status: 'FAILURE'
                        }
                        failure {
                            githubNotify description: 'Ubuntu 18 Build',  context: 'build/ubuntu18', status: 'ERROR'
                        }
                    }
                }
            }
        }
        stage('Test') {
            parallel {
                stage('Functional quick') {
                    agent {
                        label 'cluster_provisioner'
                    }
                    steps {
                        githubNotify description: 'Functional quick',  context: 'test/functional_quick', status: 'PENDING'
                        dir('install') {
                            deleteDir()
                        }
                        unstash 'CentOS-install'
                        unstash 'CentOS-build-vars'
                        sh '''bash ftest.sh quick
                              rm -rf src/tests/ftest/avocado/job-results/*/html/ "Functional quick"/
                              mkdir "Functional quick"/
                              mv install/tmp/daos.log "Functional quick"/
                              mv src/tests/ftest/avocado/job-results/** "Functional quick"/'''
                    }
                    post {
                        success {
                            githubNotify description: 'Functional quick',  context: 'test/functional_quick', status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify description: 'Functional quick',  context: 'test/functional_quick', status: 'FAILURE'
                        }
                        failure {
                            githubNotify description: 'Functional quick',  context: 'test/functional_quick', status: 'ERROR'
                        }
                        always {
                            archiveArtifacts artifacts: 'Functional quick/**'
                            junit 'Functional quick/*/results.xml'
                        }
                    }
                }
                stage('run_test.sh') {
                    agent {
                        label 'single'
                    }
                    steps {
                        githubNotify description: 'run_test.sh',  context: 'test/run_test.sh', status: 'PENDING'
                        dir('install') {
                            deleteDir()
                        }
                        unstash 'CentOS-tests'
                        unstash 'CentOS-install'
                        unstash 'CentOS-build-vars'
                        sh '''HOSTPREFIX=wolf-53 bash -x utils/run_test.sh --init
                              rm -rf run_test.sh/
                              mkdir run_test.sh/
                              mv /tmp/daos.log run_test.sh/'''
                    }
                    post {
                        success {
                            githubNotify description: 'run_test.sh',  context: 'test/run_test.sh', status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify description: 'run_test.sh',  context: 'test/run_test.sh', status: 'FAILURE'
                        }
                        failure {
                            githubNotify description: 'run_test.sh',  context: 'test/run_test.sh', status: 'ERROR'
                        }
                        always {
                            archiveArtifacts artifacts: 'run_test.sh/**'
                        }
                    }
                }
                stage('DaosTestMulti All') {
                    agent {
                        label 'cluster_provisioner'
                    }
                    steps {
                        githubNotify description: 'DaosTestMulti All',  context: 'test/daostestmulti_all', status: 'PENDING'
                        dir('install') {
                            deleteDir()
                        }
                        unstash 'CentOS-install'
                        sh '''trap 'rm -rf DaosTestMulti-All/
                                    mkdir DaosTestMulti-All/
                                    mv daos.log DaosTestMulti-All
                                    [ -f results.xml ] && mv -f results.xml DaosTestMulti-All' EXIT
                              bash DaosTestMulti.sh || true'''
                    }
                    post {
                        success {
                            githubNotify description: 'DaosTestMulti All',  context: 'test/daostestmulti_all', status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify description: 'DaosTestMulti All',  context: 'test/daostestmulti_all', status: 'FAILURE'
                        }
                        failure {
                            githubNotify description: 'DaosTestMulti All',  context: 'test/daostestmulti_all', status: 'ERROR'
                        }
                        always {
                            archiveArtifacts artifacts: 'DaosTestMulti-All/**'
                            junit allowEmptyResults: false, testResults: 'DaosTestMulti-All/results.xml'
                        }
                    }
                }
                stage('DaosTestMulti Degraded') {
                    agent {
                        label 'cluster_provisioner'
                    }
                    steps {
                        githubNotify description: 'DaosTestMulti Degraded',  context: 'test/daostestmulti_degraded', status: 'PENDING'
                        dir('install') {
                            deleteDir()
                        }
                        unstash 'CentOS-install'
                        sh '''trap 'rm -rf DaosTestMulti-Degraded/
                                    mkdir DaosTestMulti-Degraded/
                                    mv daos.log DaosTestMulti-Degraded
                                    [ -f results.xml ] && mv -f results.xml DaosTestMulti-Degraded' EXIT
                              bash DaosTestMulti.sh -d || true'''
                    }
                    post {
                        success {
                            githubNotify description: 'DaosTestMulti Degraded',  context: 'test/daostestmulti_degraded', status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify description: 'DaosTestMulti Degraded',  context: 'test/daostestmulti_degraded', status: 'FAILURE'
                        }
                        failure {
                            githubNotify description: 'DaosTestMulti Degraded',  context: 'test/daostestmulti_degraded', status: 'ERROR'
                        }
                        always {
                            archiveArtifacts artifacts: 'DaosTestMulti-Degraded/**'
                            junit allowEmptyResults: false, testResults: 'DaosTestMulti-Degraded/results.xml'
                        }
                    }
                }
                stage('DaosTestMulti Rebuild') {
                    agent {
                        label 'cluster_provisioner'
                    }
                    steps {
                        githubNotify description: 'DaosTestMulti Rebuild',  context: 'test/daostestmulti_rebuild', status: 'PENDING'
                        dir('install') {
                            deleteDir()
                        }
                        unstash 'CentOS-install'
                        sh '''trap 'rm -rf DaosTestMulti-Rebuild/
                                    mkdir DaosTestMulti-Rebuild/
                                    mv daos.log DaosTestMulti-Rebuild
                                    [ -f results.xml ] && mv -f results.xml DaosTestMulti-Rebuild' EXIT
                              bash DaosTestMulti.sh -r || true'''
                    }
                    post {
                        success {
                            githubNotify description: 'DaosTestMulti Rebuild',  context: 'test/daostestmulti_rebuild', status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify description: 'DaosTestMulti Rebuild',  context: 'test/daostestmulti_rebuild', status: 'FAILURE'
                        }
                        failure {
                            githubNotify description: 'DaosTestMulti Rebuild',  context: 'test/daostestmulti_rebuild', status: 'ERROR'
                        }
                        always {
                            archiveArtifacts artifacts: 'DaosTestMulti-Rebuild/**'
                            junit allowEmptyResults: false, testResults: 'DaosTestMulti-Rebuild/results.xml'
                        }
                    }
                }
            }
        }
    }
}
