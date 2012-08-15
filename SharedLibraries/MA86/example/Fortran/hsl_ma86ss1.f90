program hsl_ma86ss1
   use hsl_ma86_single
   use hsl_mc68_single
   use hsl_mc69_single
   implicit none

   integer, parameter :: wp = kind(0.0)

   type(mc68_control) :: control68
   type(mc68_info) :: info68

   type(ma86_keep)    :: keep
   type(ma86_control) :: control
   type(ma86_info)    :: info

   integer :: n, ne, nrhs, lmap, flag
   integer, dimension(:), allocatable  :: crow, ccol, ptr, row, order, map
   real(wp), dimension(:), allocatable :: cval, val, x
   real(wp), dimension(:,:), allocatable :: x2

   ! Read the first matrix in coordinate format
   read(*,*) n, ne
   allocate(ccol(ne)); read(*,*) ccol(:)
   allocate(crow(ne)); read(*,*) crow(:)
   allocate(cval(ne)); read(*,*) cval(:)
   ! Read the first right hand side
   allocate(x(n)); read(*,*) x(:)

   ! Convert to HSL standard format
   allocate(ptr(n+1))
   call mc69_coord_convert(HSL_MATRIX_REAL_SYM_INDEF, n, n, ne, crow, ccol, &
      ptr, row, flag, val_in=cval, val_out=val, lmap=lmap, map=map)
   call stop_on_bad_flag("mc69_coord_convert", flag)

   ! Call mc68 to find a fill reducing ordering (1=AMD)
   allocate(order(n))
   call mc68_order(1, n, ptr, row, order, control68, info68)
   call stop_on_bad_flag("mc68_order", info68%flag)

   ! Analyse
   call ma86_analyse(n, ptr, row, order, keep, control, info)
   call stop_on_bad_flag("analyse", info%flag)

   ! Factor
   call ma86_factor(n, ptr, row, val, order, keep, control, info)
   call stop_on_bad_flag("factor", info%flag)

   ! Solve
   call ma86_solve(x, order, keep, control, info)
   call stop_on_bad_flag("solve", info%flag)

   write(*,'(a)') ' Computed solution:'
   write(*,'(8f10.3)') x(1:n)

   ! Read the values of the second matrix and the new right hand sides
   read(*,*) cval(:)
   read(*,*) nrhs
   allocate(x2(n,nrhs)); read(*,*) x2(:,:)

   ! Convert the values to HSL standard form
   call mc69_set_values(HSL_MATRIX_REAL_SYM_INDEF, lmap, map, cval, &
      ptr(n+1)-1, val)

   ! Perform second factorization and solve
   call ma86_factor_solve(n, ptr, row, val, order, keep, control, info, &
      nrhs, n, x2)
   call stop_on_bad_flag("factor_solve", info%flag)

   write(*,'(a)') ' Computed solutions:'
   write(*,'(8f10.3)') x2(1:n,1)
   write(*,'(8f10.3)') x2(1:n,2)

   ! Finalize
   call ma86_finalise(keep, control)

contains
   subroutine stop_on_bad_flag(context, flag)
      character(len=*), intent(in) :: context
      integer, intent(in) :: flag

      if(flag.eq.0) return
      write(*,*) "Failure during ", context, " with flag = ", flag
      stop
   end subroutine stop_on_bad_flag
end program hsl_ma86ss1